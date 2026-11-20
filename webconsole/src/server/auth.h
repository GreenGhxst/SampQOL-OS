// Authentication and sessions.
//
// Pairing: a device-side pairing code (shown in the server log / status,
// later on the MPC screen) is exchanged for a session. Roles: viewer,
// file_manager, admin. Administrator access requires a separate password
// (PBKDF2-HMAC-SHA256 hash in the config) and is elevated per session.
// Sessions expire on idle and absolute timeouts, can be revoked, carry a CSRF
// token and an activity log. Login attempts are rate limited per client.
#pragma once
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "../util/common.h"

namespace dm {

enum class Role { none = 0, viewer = 1, file_manager = 2, admin = 3 };
const char* role_name(Role r);
Role role_from_string(const std::string& s);

struct Session {
    std::string id;                 // secret, cookie value
    std::string csrf;               // double-submit token
    Role role = Role::viewer;
    bool trusted_transport = true;  // false for plain HTTP on non-loopback: read-only viewer only
    std::string client, user_agent;
    std::int64_t created = 0, last_seen = 0, expires_absolute = 0;
    bool revoked = false;
    std::deque<json> activity;      // bounded
    json to_json(bool include_secret = false) const;
};

struct AuthConfig {
    std::string pairing_code;               // required for any session
    std::string admin_password_hash;        // "pbkdf2-sha256$<iters>$<salt-hex>$<hash-hex>"; empty = admin disabled
    std::int64_t idle_timeout_ms = 30 * 60 * 1000;
    std::int64_t absolute_timeout_ms = 12 * 60 * 60 * 1000;
    int max_failures = 5;                   // per client, then lockout
    std::int64_t lockout_ms = 60 * 1000;
    std::size_t max_sessions = 32;
};

std::string pbkdf2_sha256_hash(const std::string& password, const std::string& salt_hex, int iterations);
std::string make_password_hash(const std::string& password);             // random salt, 100k iterations
bool verify_password_hash(const std::string& password, const std::string& stored);
std::string hmac_sha256_hex(const std::string& key, const std::string& msg);

class AuthService {
public:
    explicit AuthService(AuthConfig cfg);
    // Pairing. `requested` is viewer or file_manager. Fails closed on lockout.
    Status pair(const std::string& code, Role requested, const std::string& client, const std::string& ua,
                bool trusted_transport, Session& out);
    Status elevate(const std::string& session_id, const std::string& password, const std::string& client);
    // Returns a copy of the live session or fails (expired/revoked/unknown).
    Status authenticate(const std::string& session_id, Session& out);
    bool check_csrf(const std::string& session_id, const std::string& token);
    void touch(const std::string& session_id, const std::string& activity = "");
    void revoke(const std::string& session_id);
    void revoke_all_except(const std::string& keep);
    // Revokes by the opaque handle shown in list_sessions(). Returns false when unknown.
    bool revoke_by_handle(const std::string& handle);
    static std::string handle_of(const std::string& session_id);
    void logout(const std::string& session_id) { revoke(session_id); }
    json list_sessions(const std::string& current) const;
    std::size_t prune();
    const AuthConfig& config() const { return cfg_; }
    bool admin_enabled() const { return !cfg_.admin_password_hash.empty(); }
    std::string pairing_code() const { return cfg_.pairing_code; }
    void set_pairing_code(const std::string& c) { std::lock_guard<std::mutex> lk(mu_); cfg_.pairing_code = c; }
    static bool role_at_least(Role have, Role need) { return int(have) >= int(need); }

private:
    bool locked_out(const std::string& client);
    void record_failure(const std::string& client);
    void clear_failures(const std::string& client);
    AuthConfig cfg_;
    mutable std::mutex mu_;
    std::map<std::string, Session> sessions_;
    struct Fail { int count = 0; std::int64_t until = 0; };
    std::map<std::string, Fail> failures_;
};

} // namespace dm
