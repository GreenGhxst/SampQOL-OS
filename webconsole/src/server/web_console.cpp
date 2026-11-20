#include "web_console.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include "../core/network.h"

namespace dm {

const char* kVersion = "0.1.0-dev";

// ---------------------------------------------------------------- helpers

int http_status_for(Err e) {
    switch (e) {
        case Err::none: return 200;
        case Err::invalid_argument: return 400;
        case Err::not_found: return 404;
        case Err::exists: return 409;
        case Err::forbidden: return 403;
        case Err::read_only: return 403;
        case Err::busy: return 409;
        case Err::no_space: return 507;
        case Err::io: return 500;
        case Err::unsupported: return 501;
        case Err::cancelled: return 409;
        case Err::unavailable: return 503;
        case Err::partial: return 207;
    }
    return 500;
}

void send_json(httplib::Response& res, int code, const json& j) {
    res.status = code;
    res.set_content(j.dump(), "application/json; charset=utf-8");
}

void send_status(httplib::Response& res, const Status& st, const json& extra) {
    json j = extra;
    j["ok"] = st.ok();
    j["error"] = st.ok() ? "" : err_name(st.code);
    j["message"] = st.message;
    send_json(res, http_status_for(st.code), j);
}

std::string content_disposition(const std::string& filename) {
    // RFC 5987 filename*: percent-encode UTF-8, plus an ASCII fallback.
    std::string ascii, enc;
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : filename) {
        ascii.push_back((c >= 0x20 && c < 0x7f && c != '"' && c != '\\') ? char(c) : '_');
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_') enc.push_back(char(c));
        else { enc.push_back('%'); enc.push_back(hex[c >> 4]); enc.push_back(hex[c & 15]); }
    }
    return "attachment; filename=\"" + ascii + "\"; filename*=UTF-8''" + enc;
}

std::string mime_for(const std::string& name) {
    auto p = name.rfind('.');
    std::string e = p == std::string::npos ? "" : name.substr(p + 1);
    for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    if (e == "wav") return "audio/wav";
    if (e == "aif" || e == "aiff") return "audio/aiff";
    if (e == "mp3") return "audio/mpeg";
    if (e == "flac") return "audio/flac";
    if (e == "ogg") return "audio/ogg";
    if (e == "m4a") return "audio/mp4";
    if (e == "mid" || e == "midi") return "audio/midi";
    if (e == "json") return "application/json";
    if (e == "txt" || e == "md") return "text/plain; charset=utf-8";
    if (e == "png") return "image/png";
    if (e == "jpg" || e == "jpeg") return "image/jpeg";
    return "application/octet-stream";
}

namespace {
bool is_loopback(const std::string& ip) { return ip == "127.0.0.1" || ip == "::1" || ip == "::ffff:127.0.0.1"; }
bool is_ip_literal(const std::string& h) {
    if (h.empty()) return false;
    if (h[0] == '[') return true; // IPv6 literal
    return h.find_first_not_of("0123456789.") == std::string::npos;
}
std::string strip_port(const std::string& host) {
    if (!host.empty() && host[0] == '[') { auto e = host.find(']'); return e == std::string::npos ? host : host.substr(0, e + 1); }
    auto c = host.rfind(':');
    return c == std::string::npos ? host : host.substr(0, c);
}
} // namespace

// ---------------------------------------------------------------- lifecycle

WebConsole::WebConsole(Config cfg, std::unique_ptr<SystemProbe> probe, std::unique_ptr<BlockDeviceInventory> inventory,
                       std::unique_ptr<FrameSource> screen)
    : cfg_(std::move(cfg)), screen_(std::move(screen)) {
    ::mkdir(cfg_.data_dir.c_str(), 0700);
    journal_ = std::make_unique<Journal>(cfg_.data_dir + "/journal.jsonl");
    for (const auto& t : journal_->recover()) { if (::unlink(t.c_str()) == 0) log_warn("removed interrupted temporary file " + t); }
    ops_ = std::make_unique<FileOps>(*journal_);
    registry_ = std::make_unique<VolumeRegistry>(cfg_.volumes, std::move(probe));
    jobs_ = std::make_unique<JobQueue>(std::size_t(std::max(1, cfg_.workers)), cfg_.max_queue);
    transfers_ = std::make_unique<TransferManager>(cfg_.data_dir + "/staging", cfg_.max_upload_bytes, 8);
    transfers_->reap_stale(24 * 3600 * 1000);
    intel_ = std::make_unique<Intelligence>(cfg_.data_dir, *registry_, *ops_);
    intel_->load();
    inbox_ = std::make_unique<Inbox>(cfg_.data_dir, *ops_, *intel_, *registry_);
    inbox_->load();
    format_ = std::make_unique<FormatGuard>(*registry_, *jobs_, std::move(inventory), make_simulated_format_executor(), cfg_.format_fs);
    if (cfg_.auth.pairing_code.empty()) {
        cfg_.auth.pairing_code = random_token(4);   // 8 hex chars
        log_info("pairing code (generated for this run): " + cfg_.auth.pairing_code);
    }
    auth_ = std::make_unique<AuthService>(cfg_.auth);
    tls_ = !cfg_.tls_cert.empty() && !cfg_.tls_key.empty();
    build_server();
}

WebConsole::~WebConsole() { stop(); }

void WebConsole::build_server() {
#ifdef DM_WITH_TLS
    if (tls_) {
        auto s = std::make_unique<httplib::SSLServer>(cfg_.tls_cert.c_str(), cfg_.tls_key.c_str());
        if (!s->is_valid()) { log_error("TLS certificate or key could not be loaded; refusing to start without TLS"); server_.reset(); return; }
        server_ = std::move(s);
    } else
#else
    if (tls_) { log_error("built without TLS support (DM_WITH_TLS); cannot use tls_cert/tls_key"); server_.reset(); return; }
#endif
    server_ = std::make_unique<httplib::Server>();

    server_->set_payload_max_length(cfg_.upload_chunk_max + 65536);
    server_->set_read_timeout(30, 0);
    server_->set_write_timeout(60, 0);
    server_->set_keep_alive_max_count(64);
    server_->set_default_headers({
        {"X-Content-Type-Options", "nosniff"}, {"X-Frame-Options", "DENY"}, {"Referrer-Policy", "no-referrer"},
        {"Content-Security-Policy", "default-src 'self'; img-src 'self' data: blob:; media-src 'self' blob:; style-src 'self'; script-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'"},
        {"Permissions-Policy", "camera=(), microphone=(), geolocation=()"},
        {"Server", "diskmode-webconsole"}});
    server_->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        if (!host_ok(req)) { send_json(res, 421, json{{"ok", false}, {"error", "misdirected"}, {"message", "unexpected Host header"}}); return httplib::Server::HandlerResponse::Handled; }
        if (req.path.rfind("/api/", 0) == 0) res.set_header("Cache-Control", "no-store");
        return httplib::Server::HandlerResponse::Unhandled;
    });
    server_->set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        std::string what = "internal error";
        try { if (ep) std::rethrow_exception(ep); } catch (const std::exception& e) { what = e.what(); } catch (...) {}
        log_error("unhandled exception in request: " + what);
        send_json(res, 500, json{{"ok", false}, {"error", "io"}, {"message", "internal error"}});
    });
    server_->set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (res.status == 404 && req.path.rfind("/api/", 0) == 0) send_json(res, 404, json{{"ok", false}, {"error", "not_found"}, {"message", "no such endpoint"}});
        else if (res.status == 413) send_json(res, 413, json{{"ok", false}, {"error", "invalid_argument"}, {"message", "request body too large"}});
    });
    register_core_routes();
    register_extra_routes();
    if (!cfg_.web_root.empty()) {
        struct stat st{};
        if (::stat(cfg_.web_root.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            server_->set_mount_point("/", cfg_.web_root);
            server_->set_file_extension_and_mimetype_mapping("js", "application/javascript; charset=utf-8");
            server_->set_file_extension_and_mimetype_mapping("css", "text/css; charset=utf-8");
            server_->set_file_extension_and_mimetype_mapping("svg", "image/svg+xml");
            server_->set_file_extension_and_mimetype_mapping("webmanifest", "application/manifest+json");
        } else log_warn("web_root does not exist: " + cfg_.web_root);
    }
}

bool WebConsole::start() {
    if (!server_) return false;
    std::string host = cfg_.bind_host;
    if (!tls_ && !cfg_.allow_plain_http && host != "127.0.0.1" && host != "::1" && host != "localhost") {
        log_warn("no TLS configured: binding to 127.0.0.1 only (use --allow-plain-http for viewer-only plain HTTP)");
        host = "127.0.0.1";
    }
    if (cfg_.port == 0) port_ = server_->bind_to_any_port(host);
    else port_ = server_->bind_to_port(host, cfg_.port) ? cfg_.port : -1;
    if (port_ <= 0) { log_error("cannot bind " + host + ":" + std::to_string(cfg_.port)); return false; }
    started_ms_ = now_ms();
    thread_ = std::thread([this] { server_->listen_after_bind(); });
    start_background();
    log_info(std::string("listening on ") + (tls_ ? "https://" : "http://") + host + ":" + std::to_string(port_) + (tls_ ? "" : " (plain HTTP)"));
    for (int i = 0; i < 200 && !server_->is_running(); ++i) ::usleep(5000);
    return true;
}

void WebConsole::stop() {
    if (stopping_.exchange(true)) return;
    if (server_) server_->stop();
    if (thread_.joinable()) thread_.join();
    if (housekeeping_.joinable()) housekeeping_.join();
    if (jobs_) jobs_->shutdown();
    if (intel_) intel_->save();
    log_info("stopped");
}

void WebConsole::start_background() {
    housekeeping_ = std::thread([this] {
        int tick = 0;
        while (!stopping_) {
            ::usleep(250000);
            if (++tick % 20 != 0) continue;             // every 5 s
            auth_->prune();
            refresh_volumes_throttled();
            if (tick % 240 == 0) transfers_->reap_stale(24 * 3600 * 1000);
        }
    });
    if (cfg_.index_on_start) {
        for (const auto& v : registry_->list()) {
            if (!v.mounted) continue;
            std::string id;
            jobs_->submit("index", "system", [this, vid = v.id](Job& j) {
                std::size_t n = 0; Status st = intel_->scan(vid, &j.cancel, n);
                j.detail = json{{"volume", vid}, {"indexed", n}};
                if (!st.ok()) { j.state = st.code == Err::cancelled ? "cancelled" : "failed"; j.error = st.message; }
            }, id);
        }
    }
}

void WebConsole::refresh_volumes_throttled() {
    std::lock_guard<std::mutex> lk(refresh_mu_);
    if (now_ms() - last_refresh_ms_ < 2000) return;
    registry_->refresh();
    last_refresh_ms_ = now_ms();
}

json WebConsole::status_json() {
    std::uint64_t rss_kb = 0;
    { std::ifstream f("/proc/self/statm"); std::uint64_t size = 0, rss = 0; if (f >> size >> rss) rss_kb = rss * std::uint64_t(::sysconf(_SC_PAGESIZE)) / 1024; }
    return json{{"version", kVersion}, {"uptime_s", (now_ms() - started_ms_) / 1000}, {"tls", tls_}, {"port", port_},
                {"rss_kb", rss_kb}, {"screen_source", screen_ ? screen_->name() : "none"},
                {"screen_simulated", screen_ ? screen_->simulated() : false}, {"format_simulation", format_->simulation()},
                {"inbox_items", inbox_->count()}};
}

// ---------------------------------------------------------------- request helpers

bool WebConsole::host_ok(const httplib::Request& req) const {
    std::string host = strip_port(req.get_header_value("Host"));
    if (host.empty()) return false;
    for (auto& c : host) c = (char)std::tolower((unsigned char)c);
    if (is_ip_literal(host) || host == "localhost") return true;
    if (host.size() > 6 && host.compare(host.size() - 6, 6, ".local") == 0) return true;
    return std::find(cfg_.allowed_hosts.begin(), cfg_.allowed_hosts.end(), host) != cfg_.allowed_hosts.end();
}

bool WebConsole::transport_trusted(const httplib::Request& req) const { return tls_ || is_loopback(req.remote_addr); }

std::string WebConsole::cookie(const httplib::Request& req, const std::string& name) const {
    std::string c = req.get_header_value("Cookie");
    std::size_t i = 0;
    while (i < c.size()) {
        while (i < c.size() && (c[i] == ' ' || c[i] == ';')) ++i;
        auto e = c.find(';', i); if (e == std::string::npos) e = c.size();
        auto eq = c.find('=', i);
        if (eq != std::string::npos && eq < e && c.substr(i, eq - i) == name) return c.substr(eq + 1, e - eq - 1);
        i = e + 1;
    }
    return "";
}

void WebConsole::set_session_cookies(httplib::Response& res, const Session& s) {
    std::string attrs = "; Path=/; HttpOnly; SameSite=Strict" + std::string(tls_ ? "; Secure" : "");
    res.set_header("Set-Cookie", "dm_session=" + s.id + attrs);
}
void WebConsole::clear_session_cookies(httplib::Response& res) {
    res.set_header("Set-Cookie", "dm_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
}

bool WebConsole::require(const httplib::Request& req, httplib::Response& res, Role need, bool mutating, Ctx& ctx) {
    ctx.client = req.remote_addr;
    ctx.trusted = transport_trusted(req);
    std::string sid = cookie(req, "dm_session");
    Status st = auth_->authenticate(sid, ctx.session);
    if (!st.ok()) { clear_session_cookies(res); send_json(res, 401, json{{"ok", false}, {"error", "unauthenticated"}, {"message", st.message}}); return false; }
    if (!ctx.trusted && ctx.session.role != Role::viewer) {
        // Session was created over a trusted transport but is now used over an untrusted one: refuse.
        send_json(res, 403, json{{"ok", false}, {"error", "forbidden"}, {"message", "HTTPS is required for this session"}}); return false;
    }
    if (mutating) {
        // CSRF: double-submit header plus a fetch-metadata / origin check.
        std::string tok = req.get_header_value("X-DM-CSRF");
        if (!auth_->check_csrf(sid, tok)) { send_json(res, 403, json{{"ok", false}, {"error", "csrf"}, {"message", "missing or invalid CSRF token"}}); return false; }
        std::string site = req.get_header_value("Sec-Fetch-Site");
        if (!site.empty() && site != "same-origin" && site != "none") { send_json(res, 403, json{{"ok", false}, {"error", "csrf"}, {"message", "cross-site request refused"}}); return false; }
        std::string origin = req.get_header_value("Origin");
        if (!origin.empty() && origin != "null") {
            std::string host = req.get_header_value("Host");
            auto p = origin.find("://");
            std::string ohost = p == std::string::npos ? origin : origin.substr(p + 3);
            if (ohost != host) { send_json(res, 403, json{{"ok", false}, {"error", "csrf"}, {"message", "origin does not match"}}); return false; }
        }
        if (!ctx.trusted) { send_json(res, 403, json{{"ok", false}, {"error", "forbidden"}, {"message", "modifying operations require HTTPS"}}); return false; }
    }
    if (!AuthService::role_at_least(ctx.session.role, need)) {
        send_json(res, 403, json{{"ok", false}, {"error", "forbidden"}, {"message", std::string("requires role ") + role_name(need)}}); return false;
    }
    auth_->touch(sid, mutating ? req.method + " " + req.path : "");
    return true;
}

bool WebConsole::volume_or_error(const std::string& id, httplib::Response& res, VolumeInfo& out) {
    if (!VolumeRegistry::valid_id(id)) { send_status(res, Status::fail(Err::invalid_argument, "bad volume id")); return false; }
    refresh_volumes_throttled();
    auto v = registry_->get(id);
    if (!v) { send_status(res, Status::fail(Err::not_found, "unknown volume: " + id)); return false; }
    out = *v;
    return true;
}

bool WebConsole::rel_or_error(const std::string& p, httplib::Response& res, RelPath& out) {
    Status st = parse_rel_path(p, out);
    if (!st.ok()) { send_status(res, st); return false; }
    return true;
}

json WebConsole::body_json(const httplib::Request& req, httplib::Response& res, bool& ok) {
    ok = false;
    if (req.body.size() > 1 << 20) { send_status(res, Status::fail(Err::invalid_argument, "request body too large")); return json(); }
    json j = json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { send_status(res, Status::fail(Err::invalid_argument, "body must be a JSON object")); return json(); }
    ok = true;
    return j;
}

// ---------------------------------------------------------------- core routes

void WebConsole::register_core_routes() {
    auto& S = *server_;

    S.Get("/api/v1/health", [this](const httplib::Request&, httplib::Response& res) {
        send_json(res, 200, json{{"ok", true}, {"service", "diskmode-webconsole"}, {"version", kVersion}, {"tls", tls_}});
    });

    S.Get("/api/v1/auth/status", [this](const httplib::Request& req, httplib::Response& res) {
        Session s; bool authed = auth_->authenticate(cookie(req, "dm_session"), s).ok();
        bool trusted = transport_trusted(req);
        json j{{"authenticated", authed}, {"trusted_transport", trusted}, {"tls", tls_}, {"admin_enabled", auth_->admin_enabled()},
               {"version", kVersion}, {"screen_simulated", screen_ ? screen_->simulated() : false}, {"format_simulation", true}};
        if (authed) { j["role"] = role_name(s.role); j["csrf"] = s.csrf; j["session"] = s.to_json(false); }
        if (!trusted) j["warning"] = "This connection is not HTTPS. Only read-only viewing is possible.";
        send_json(res, 200, j);
    });

    S.Post("/api/v1/auth/pair", [this](const httplib::Request& req, httplib::Response& res) {
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        std::string code = b.value("code", ""); Role role = role_from_string(b.value("role", "file_manager"));
        if (code.size() > 128) { send_status(res, Status::fail(Err::invalid_argument, "code too long")); return; }
        Session s;
        Status st = auth_->pair(code, role == Role::none ? Role::file_manager : role, req.remote_addr, req.get_header_value("User-Agent"), transport_trusted(req), s);
        if (!st.ok()) { log_warn("pairing refused from " + req.remote_addr + ": " + st.message); send_status(res, st); return; }
        set_session_cookies(res, s);
        log_info("session paired from " + req.remote_addr + " as " + role_name(s.role));
        send_json(res, 200, json{{"ok", true}, {"role", role_name(s.role)}, {"csrf", s.csrf}, {"trusted_transport", s.trusted_transport}, {"session", s.to_json(false)}});
    });

    S.Post("/api/v1/auth/elevate", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        Status st = auth_->elevate(c.session.id, b.value("password", ""), req.remote_addr);
        if (!st.ok()) log_warn("admin elevation refused from " + req.remote_addr + ": " + st.message);
        send_status(res, st, json{{"role", st.ok() ? "admin" : role_name(c.session.role)}});
    });

    S.Post("/api/v1/auth/logout", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        auth_->logout(c.session.id); clear_session_cookies(res);
        send_json(res, 200, json{{"ok", true}});
    });

    S.Get("/api/v1/session", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        send_json(res, 200, json{{"ok", true}, {"session", c.session.to_json(false)}, {"csrf", c.session.csrf}});
    });
    S.Get("/api/v1/session/list", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::admin, false, c)) return;
        send_json(res, 200, json{{"ok", true}, {"sessions", auth_->list_sessions(c.session.id)}});
    });
    S.Post("/api/v1/session/revoke", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::admin, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        bool done = auth_->revoke_by_handle(b.value("handle", ""));
        send_status(res, done ? Status::good() : Status::fail(Err::not_found, "no such session"));
    });
    S.Post("/api/v1/session/revoke_others", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::admin, true, c)) return;
        auth_->revoke_all_except(c.session.id);
        send_json(res, 200, json{{"ok", true}});
    });

    S.Get("/api/v1/volumes", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        refresh_volumes_throttled();
        json arr = json::array();
        for (const auto& v : registry_->list()) arr.push_back(v.to_json());
        send_json(res, 200, json{{"ok", true}, {"volumes", arr}});
    });
    S.Post("/api/v1/volumes/refresh", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        registry_->refresh();
        json arr = json::array();
        for (const auto& v : registry_->list()) arr.push_back(v.to_json());
        send_json(res, 200, json{{"ok", true}, {"volumes", arr}});
    });

    S.Get("/api/v1/files", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        VolumeInfo v; RelPath p;
        if (!volume_or_error(req.get_param_value("volume"), res, v) || !rel_or_error(req.get_param_value("path"), res, p)) return;
        ListOptions o;
        o.offset = std::strtoul(req.get_param_value("offset").c_str(), nullptr, 10);
        o.limit = req.has_param("limit") ? std::strtoul(req.get_param_value("limit").c_str(), nullptr, 10) : 500;
        o.sort = req.has_param("sort") ? req.get_param_value("sort") : "name";
        o.descending = req.get_param_value("desc") == "1";
        o.filter = req.get_param_value("filter").substr(0, 100);
        o.kind = req.get_param_value("kind");
        o.show_hidden = req.get_param_value("hidden") == "1";
        Listing l;
        Status st = ops_->list(v, p, o, l);
        if (!st.ok()) { send_status(res, st); return; }
        json entries = json::array();
        for (const auto& e : l.entries) entries.push_back(e.to_json());
        send_json(res, 200, json{{"ok", true}, {"volume", v.id}, {"path", p.str()}, {"entries", entries}, {"total", l.total},
                                 {"offset", o.offset}, {"truncated", l.truncated}, {"writable", v.writable()}});
    });

    S.Get("/api/v1/files/stat", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        VolumeInfo v; RelPath p;
        if (!volume_or_error(req.get_param_value("volume"), res, v) || !rel_or_error(req.get_param_value("path"), res, p)) return;
        Entry e; Status st = ops_->stat(v, p, e);
        if (!st.ok()) { send_status(res, st); return; }
        json j = e.to_json(); j["ok"] = true; j["volume"] = v.id; j["path"] = p.str();
        j["tags"] = intel_->tags(v.id, p.str());
        if (e.type == "file" && file_kind(e.name) == "audio") {
            ScopedFd fd; std::uint64_t size = 0;
            if (ops_->open_read(v, p, fd, size).ok()) j["wav"] = wav_probe(fd.get()).to_json();
        }
        send_json(res, 200, j);
    });

    S.Get("/api/v1/files/download", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        VolumeInfo v; RelPath p;
        if (!volume_or_error(req.get_param_value("volume"), res, v) || !rel_or_error(req.get_param_value("path"), res, p)) return;
        auto fd = std::make_shared<ScopedFd>(); std::uint64_t size = 0;
        Status st = ops_->open_read(v, p, *fd, size);
        if (!st.ok()) { send_status(res, st); return; }
        intel_->touch_recent(v.id, p.str());
        bool inline_view = req.get_param_value("inline") == "1";
        if (!inline_view) res.set_header("Content-Disposition", content_disposition(p.leaf()));
        res.set_header("Accept-Ranges", "bytes");
        res.set_content_provider(size, mime_for(p.leaf()),
            [fd](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
                char buf[65536];
                std::size_t n = std::min(length, sizeof buf);
                ssize_t r = ::pread(fd->get(), buf, n, off_t(offset));
                if (r <= 0) return false;
                return sink.write(buf, std::size_t(r));
            });
    });

    S.Get("/api/v1/files/peaks", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        VolumeInfo v; RelPath p;
        if (!volume_or_error(req.get_param_value("volume"), res, v) || !rel_or_error(req.get_param_value("path"), res, p)) return;
        ScopedFd fd; std::uint64_t size = 0;
        Status st = ops_->open_read(v, p, fd, size); if (!st.ok()) { send_status(res, st); return; }
        WavInfo w = wav_probe(fd.get());
        std::vector<float> peaks;
        std::size_t buckets = req.has_param("buckets") ? std::strtoul(req.get_param_value("buckets").c_str(), nullptr, 10) : 400;
        st = wav_peaks(fd.get(), w, buckets, peaks);
        if (!st.ok()) { send_status(res, st, json{{"wav", w.to_json()}}); return; }
        send_json(res, 200, json{{"ok", true}, {"wav", w.to_json()}, {"peaks", peaks}});
    });

    S.Get("/api/v1/files/hash", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        VolumeInfo v; RelPath p;
        if (!volume_or_error(req.get_param_value("volume"), res, v) || !rel_or_error(req.get_param_value("path"), res, p)) return;
        Entry e; Status st = ops_->stat(v, p, e); if (!st.ok()) { send_status(res, st); return; }
        if (e.type != "file") { send_status(res, Status::fail(Err::invalid_argument, "not a file")); return; }
        if (e.size <= (64u << 20)) {
            std::string hex; OpContext ctx; st = ops_->sha256(v, p, hex, ctx);
            if (!st.ok()) { send_status(res, st); return; }
            send_json(res, 200, json{{"ok", true}, {"sha256", hex}, {"size", e.size}});
            return;
        }
        std::string id;
        st = jobs_->submit("hash", c.session.id, [this, v, p](Job& j) {
            std::string hex; OpContext ctx; ctx.cancel = &j.cancel;
            ctx.progress = [&j](std::uint64_t d, std::uint64_t t) { j.done = d; j.total = t; };
            Status s = ops_->sha256(v, p, hex, ctx);
            if (s.ok()) j.detail = json{{"sha256", hex}}; else { j.state = s.code == Err::cancelled ? "cancelled" : "failed"; j.error = s.message; }
        }, id, {JobItem{v.id + ":" + p.str(), "", "queued", ""}});
        if (!st.ok()) { send_status(res, st); return; }
        send_json(res, 202, json{{"ok", true}, {"job", id}});
    });

    S.Post("/api/v1/files/mkdir", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        VolumeInfo v; RelPath p;
        if (!volume_or_error(b.value("volume", ""), res, v) || !rel_or_error(b.value("path", ""), res, p)) return;
        send_status(res, ops_->mkdir(v, p), json{{"path", p.str()}});
    });

    S.Post("/api/v1/files/rename", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        VolumeInfo v; RelPath p;
        if (!volume_or_error(b.value("volume", ""), res, v) || !rel_or_error(b.value("path", ""), res, p)) return;
        std::string name = b.value("name", "");
        Status st = ops_->rename(v, p, name);
        if (st.ok()) intel_->invalidate(v.id, p.str());
        send_status(res, st, json{{"path", p.parent().child(name).str()}});
    });

    S.Post("/api/v1/files/copy", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        std::string id = submit_batch("copy", c, b.value("items", json::array()), b.value("dest", json::object()), conflict_from_string(b.value("conflict", "fail")), false, res);
        if (!id.empty()) send_json(res, 202, json{{"ok", true}, {"job", id}});
    });
    S.Post("/api/v1/files/move", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        std::string id = submit_batch("move", c, b.value("items", json::array()), b.value("dest", json::object()), conflict_from_string(b.value("conflict", "fail")), false, res);
        if (!id.empty()) send_json(res, 202, json{{"ok", true}, {"job", id}});
    });
    S.Post("/api/v1/files/delete", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        std::string id = submit_batch("delete", c, b.value("items", json::array()), json::object(), Conflict::fail, b.value("permanent", false), res);
        if (!id.empty()) send_json(res, 202, json{{"ok", true}, {"job", id}});
    });

    S.Get("/api/v1/files/trash", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        VolumeInfo v; if (!volume_or_error(req.get_param_value("volume"), res, v)) return;
        std::vector<TrashItem> items; Status st = ops_->list_trash(v, items);
        if (!st.ok()) { send_status(res, st); return; }
        json arr = json::array(); for (auto& t : items) arr.push_back(t.to_json());
        send_json(res, 200, json{{"ok", true}, {"items", arr}});
    });
    S.Post("/api/v1/files/trash/restore", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        VolumeInfo v; if (!volume_or_error(b.value("volume", ""), res, v)) return;
        std::string restored; Status st = ops_->restore(v, b.value("id", ""), &restored);
        send_status(res, st, json{{"path", restored}});
    });
    S.Post("/api/v1/files/trash/purge", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        VolumeInfo v; if (!volume_or_error(b.value("volume", ""), res, v)) return;
        send_status(res, ops_->purge_trash_item(v, b.value("id", "")));
    });

    S.Post("/api/v1/files/batch_rename/plan", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        VolumeInfo v; RelPath dir;
        if (!volume_or_error(b.value("volume", ""), res, v) || !rel_or_error(b.value("dir", ""), res, dir)) return;
        auto names = b.value("names", std::vector<std::string>{});
        if (names.size() > 1000) { send_status(res, Status::fail(Err::invalid_argument, "too many files")); return; }
        auto plan = intel_->rename_plan(v, dir, names, b.value("pattern", "{name}{ext}"), b.value("start", 1));
        json arr = json::array(); for (auto& p : plan) arr.push_back(json{{"from", p.from}, {"to", p.to}, {"problem", p.problem}});
        send_json(res, 200, json{{"ok", true}, {"plan", arr}});
    });
    S.Post("/api/v1/files/batch_rename/apply", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        VolumeInfo v; RelPath dir;
        if (!volume_or_error(b.value("volume", ""), res, v) || !rel_or_error(b.value("dir", ""), res, dir)) return;
        auto names = b.value("names", std::vector<std::string>{});
        if (names.size() > 1000) { send_status(res, Status::fail(Err::invalid_argument, "too many files")); return; }
        auto plan = intel_->rename_plan(v, dir, names, b.value("pattern", "{name}{ext}"), b.value("start", 1));
        send_json(res, 200, json{{"ok", true}, {"results", intel_->rename_apply(v, dir, plan)}});
    });
}

std::string WebConsole::submit_batch(const std::string& type, const Ctx& ctx, const json& items, const json& dest,
                                     Conflict conflict, bool permanent, httplib::Response& res) {
    if (!items.is_array() || items.empty() || items.size() > 500) { send_status(res, Status::fail(Err::invalid_argument, "items must be a non-empty array (max 500)")); return ""; }
    struct Item { VolumeInfo v; RelPath p; };
    std::vector<Item> parsed; std::vector<JobItem> jitems;
    VolumeInfo dv; RelPath dd;
    if (type != "delete") {
        if (!volume_or_error(dest.value("volume", ""), res, dv) || !rel_or_error(dest.value("path", ""), res, dd)) return "";
        Status st = PathAuthorizer::check_write(dv, dd.child("x"));
        if (!st.ok()) { send_status(res, st); return ""; }
    }
    for (const auto& it : items) {
        Item x;
        if (!it.is_object() || !volume_or_error(it.value("volume", ""), res, x.v) || !rel_or_error(it.value("path", ""), res, x.p)) return "";
        if (x.p.empty()) { send_status(res, Status::fail(Err::invalid_argument, "cannot operate on a volume root")); return ""; }
        Status st = type == "copy" ? PathAuthorizer::check_read(x.v) : PathAuthorizer::check_write(x.v, x.p);
        if (!st.ok()) { send_status(res, st); return ""; }
        jitems.push_back(JobItem{x.v.id + ":" + x.p.str(), type == "delete" ? "" : dv.id + ":" + dd.child(x.p.leaf()).str(), "queued", ""});
        parsed.push_back(std::move(x));
    }
    std::string id;
    Status st = jobs_->submit(type, ctx.session.id, [this, type, parsed, dv, dd, conflict, permanent](Job& j) {
        std::uint64_t base = 0;
        for (std::size_t i = 0; i < parsed.size(); ++i) {
            if (j.cancel) { jobs_->set_item(j, i, "cancelled"); continue; }
            jobs_->set_item(j, i, "running");
            OpContext ctx; ctx.cancel = &j.cancel;
            std::uint64_t last = 0;
            ctx.progress = [&](std::uint64_t d, std::uint64_t) { j.done = base + d; last = d; };
            Status s;
            std::string fname;
            if (type == "copy") s = ops_->copy(parsed[i].v, parsed[i].p, dv, dd, "", conflict, ctx, &fname);
            else if (type == "move") s = ops_->move(parsed[i].v, parsed[i].p, dv, dd, "", conflict, ctx, &fname);
            else { std::string tid; s = ops_->remove(parsed[i].v, parsed[i].p, permanent, &tid); if (s.ok() && !tid.empty()) j.detail["trash_ids"].push_back(tid); }
            base += last;
            if (s.ok()) { jobs_->set_item(j, i, "done"); if (type != "copy") intel_->invalidate(parsed[i].v.id, parsed[i].p.str()); }
            else if (s.code == Err::cancelled) jobs_->set_item(j, i, "cancelled");
            else jobs_->set_item(j, i, "failed", s.message);
        }
    }, id, jitems);
    if (!st.ok()) { send_status(res, st); return ""; }
    return id;
}

} // namespace dm
