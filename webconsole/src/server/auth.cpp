#include "auth.h"
#include <cstring>
#include "../util/sha256.h"

namespace dm {

const char* role_name(Role r) {
    switch (r) { case Role::none: return "none"; case Role::viewer: return "viewer"; case Role::file_manager: return "file_manager"; case Role::admin: return "admin"; }
    return "none";
}
Role role_from_string(const std::string& s) {
    if (s == "admin") return Role::admin;
    if (s == "file_manager") return Role::file_manager;
    if (s == "viewer") return Role::viewer;
    return Role::none;
}

json Session::to_json(bool include_secret) const {
    json j{{"role", role_name(role)}, {"trusted_transport", trusted_transport}, {"client", client}, {"user_agent", user_agent},
           {"created", created}, {"last_seen", last_seen}, {"expires_absolute", expires_absolute}, {"revoked", revoked},
           {"id_prefix", id.substr(0, 8)}, {"activity", activity}};
    if (include_secret) j["id"] = id;
    return j;
}

// ---- HMAC / PBKDF2 on top of our SHA-256
static std::array<std::uint8_t, 32> hmac_raw(const std::string& key, const std::uint8_t* msg, std::size_t len) {
    std::uint8_t k[64] = {0};
    if (key.size() > 64) { Sha256 h; h.update(key.data(), key.size()); auto d = h.digest(); std::memcpy(k, d.data(), 32); }
    else std::memcpy(k, key.data(), key.size());
    std::uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Sha256 inner; inner.update(ipad, 64); inner.update(msg, len); auto id = inner.digest();
    Sha256 outer; outer.update(opad, 64); outer.update(id.data(), 32);
    return outer.digest();
}
std::string hmac_sha256_hex(const std::string& key, const std::string& msg) {
    auto d = hmac_raw(key, reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size());
    return to_hex(d.data(), d.size());
}
static std::string from_hex(const std::string& h) {
    std::string out;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(char(std::stoi(h.substr(i, 2), nullptr, 16)));
    return out;
}
std::string pbkdf2_sha256_hash(const std::string& password, const std::string& salt_hex, int iterations) {
    std::string salt = from_hex(salt_hex);
    std::vector<std::uint8_t> block(salt.begin(), salt.end());
    block.push_back(0); block.push_back(0); block.push_back(0); block.push_back(1);   // INT(1), one 32-byte block
    auto u = hmac_raw(password, block.data(), block.size());
    auto t = u;
    for (int i = 1; i < iterations; ++i) { u = hmac_raw(password, u.data(), u.size()); for (int j = 0; j < 32; ++j) t[j] ^= u[j]; }
    return to_hex(t.data(), t.size());
}
std::string make_password_hash(const std::string& password) {
    std::string salt = random_token(16);
    const int iters = 100000;
    return "pbkdf2-sha256$" + std::to_string(iters) + "$" + salt + "$" + pbkdf2_sha256_hash(password, salt, iters);
}
bool verify_password_hash(const std::string& password, const std::string& stored) {
    // format: pbkdf2-sha256$iters$salt$hash
    std::vector<std::string> parts; std::size_t i = 0;
    while (true) { auto j = stored.find('$', i); parts.push_back(stored.substr(i, j == std::string::npos ? std::string::npos : j - i)); if (j == std::string::npos) break; i = j + 1; }
    if (parts.size() != 4 || parts[0] != "pbkdf2-sha256") return false;
    int iters = std::atoi(parts[1].c_str());
    if (iters < 1000 || iters > 10000000 || parts[2].size() < 16) return false;
    return constant_time_equal(pbkdf2_sha256_hash(password, parts[2], iters), parts[3]);
}

// ---- sessions

AuthService::AuthService(AuthConfig cfg) : cfg_(std::move(cfg)) {}

bool AuthService::locked_out(const std::string& client) {
    auto it = failures_.find(client);
    return it != failures_.end() && it->second.until > now_ms();
}
void AuthService::record_failure(const std::string& client) {
    auto& f = failures_[client];
    if (f.until && f.until < now_ms()) f.count = 0;
    if (++f.count >= cfg_.max_failures) { f.until = now_ms() + cfg_.lockout_ms; f.count = 0; }
}
void AuthService::clear_failures(const std::string& client) { failures_.erase(client); }

Status AuthService::pair(const std::string& code, Role requested, const std::string& client, const std::string& ua,
                         bool trusted_transport, Session& out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (locked_out(client)) return Status::fail(Err::busy, "too many attempts; try again later");
    if (requested != Role::viewer && requested != Role::file_manager) return Status::fail(Err::invalid_argument, "role must be viewer or file_manager");
    if (cfg_.pairing_code.empty() || !constant_time_equal(code, cfg_.pairing_code)) { record_failure(client); return Status::fail(Err::forbidden, "pairing code rejected"); }
    clear_failures(client);
    std::size_t live = 0;
    for (auto& [id, s] : sessions_) if (!s.revoked) ++live;
    if (live >= cfg_.max_sessions) return Status::fail(Err::busy, "too many active sessions; revoke one first");
    Session s;
    s.id = random_token(32); s.csrf = random_token(32);
    s.role = trusted_transport ? requested : Role::viewer;     // never grant write access over plain HTTP
    s.trusted_transport = trusted_transport; s.client = client; s.user_agent = ua.substr(0, 120);
    s.created = s.last_seen = now_ms(); s.expires_absolute = s.created + cfg_.absolute_timeout_ms;
    s.activity.push_back(json{{"ts", s.created}, {"event", "paired"}, {"role", role_name(s.role)}});
    sessions_[s.id] = s;
    out = s;
    return Status::good();
}

Status AuthService::elevate(const std::string& session_id, const std::string& password, const std::string& client) {
    std::lock_guard<std::mutex> lk(mu_);
    if (locked_out(client)) return Status::fail(Err::busy, "too many attempts; try again later");
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || it->second.revoked) return Status::fail(Err::forbidden, "no session");
    if (!it->second.trusted_transport) return Status::fail(Err::forbidden, "administrator access requires HTTPS");
    if (cfg_.admin_password_hash.empty()) return Status::fail(Err::unsupported, "administrator access is not configured on this device");
    if (!verify_password_hash(password, cfg_.admin_password_hash)) { record_failure(client); return Status::fail(Err::forbidden, "administrator password rejected"); }
    clear_failures(client);
    it->second.role = Role::admin;
    it->second.activity.push_back(json{{"ts", now_ms()}, {"event", "elevated"}, {"role", "admin"}});
    return Status::good();
}

Status AuthService::authenticate(const std::string& session_id, Session& out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (session_id.size() != 64) return Status::fail(Err::forbidden, "not authenticated");
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return Status::fail(Err::forbidden, "not authenticated");
    Session& s = it->second;
    std::int64_t now = now_ms();
    if (s.revoked) return Status::fail(Err::forbidden, "session revoked");
    if (now > s.expires_absolute || now - s.last_seen > cfg_.idle_timeout_ms) { s.revoked = true; return Status::fail(Err::forbidden, "session expired"); }
    out = s;
    return Status::good();
}

bool AuthService::check_csrf(const std::string& session_id, const std::string& token) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(session_id);
    return it != sessions_.end() && !token.empty() && constant_time_equal(it->second.csrf, token);
}

void AuthService::touch(const std::string& session_id, const std::string& activity) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return;
    it->second.last_seen = now_ms();
    if (!activity.empty()) {
        it->second.activity.push_back(json{{"ts", it->second.last_seen}, {"event", activity}});
        while (it->second.activity.size() > 50) it->second.activity.pop_front();
    }
}

void AuthService::revoke(const std::string& session_id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) { it->second.revoked = true; it->second.activity.push_back(json{{"ts", now_ms()}, {"event", "revoked"}}); }
}
void AuthService::revoke_all_except(const std::string& keep) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [id, s] : sessions_) if (id != keep) s.revoked = true;
}

json AuthService::list_sessions(const std::string& current) const {
    std::lock_guard<std::mutex> lk(mu_);
    json out = json::array();
    for (const auto& [id, s] : sessions_) {
        json j = s.to_json(false);
        j["current"] = (id == current);
        j["handle"] = handle_of(id);
        out.push_back(j);
    }
    return out;
}

std::string AuthService::handle_of(const std::string& session_id) { return hmac_sha256_hex("session-handle", session_id).substr(0, 16); }

bool AuthService::revoke_by_handle(const std::string& handle) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [id, s] : sessions_) if (handle_of(id) == handle) { s.revoked = true; s.activity.push_back(json{{"ts", now_ms()}, {"event", "revoked"}}); return true; }
    return false;
}

std::size_t AuthService::prune() {
    std::lock_guard<std::mutex> lk(mu_);
    std::size_t n = 0; std::int64_t now = now_ms();
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        Session& s = it->second;
        bool dead = s.revoked || now > s.expires_absolute || now - s.last_seen > cfg_.idle_timeout_ms;
        if (dead && now - s.last_seen > 60 * 60 * 1000) { it = sessions_.erase(it); ++n; } else ++it;
    }
    return n;
}

} // namespace dm
