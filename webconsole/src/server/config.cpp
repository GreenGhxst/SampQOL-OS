#include "config.h"
#include <cstring>
#include <fstream>

namespace dm {

Config Config::from_json(const json& j) {
    Config c;
    c.bind_host = j.value("bind_host", c.bind_host);
    c.port = j.value("port", c.port);
    c.tls_cert = j.value("tls_cert", ""); c.tls_key = j.value("tls_key", "");
    c.allow_plain_http = j.value("allow_plain_http", false);
    c.data_dir = j.value("data_dir", ""); c.web_root = j.value("web_root", "");
    c.application_medium_mount = j.value("application_medium_mount", "");
    if (j.contains("volumes")) c.volumes = VolumeRegistry::rules_from_json(j["volumes"]);
    if (j.contains("auth")) {
        const auto& a = j["auth"];
        c.auth.pairing_code = a.value("pairing_code", "");
        c.auth.admin_password_hash = a.value("admin_password_hash", "");
        c.auth.idle_timeout_ms = a.value("idle_timeout_minutes", 30) * 60000ll;
        c.auth.absolute_timeout_ms = a.value("absolute_timeout_hours", 12) * 3600000ll;
        c.auth.max_failures = a.value("max_failures", 5);
        c.auth.lockout_ms = a.value("lockout_seconds", 60) * 1000ll;
        c.auth.max_sessions = a.value("max_sessions", 32);
    }
    c.max_upload_bytes = j.value("max_upload_bytes", c.max_upload_bytes);
    c.upload_chunk_max = j.value("upload_chunk_max", c.upload_chunk_max);
    c.workers = j.value("workers", c.workers);
    c.max_queue = j.value("max_queue", c.max_queue);
    c.screen_max_fps = j.value("screen_max_fps", c.screen_max_fps);
    c.screen_source = j.value("screen_source", c.screen_source);
    c.format_fs = j.value("format_fs", c.format_fs);
    c.allowed_hosts = j.value("allowed_hosts", c.allowed_hosts);
    c.index_on_start = j.value("index_on_start", false);
    c.log_level = j.value("log_level", "info");
    return c;
}

json Config::to_json() const {
    json vols = json::array();
    for (const auto& r : volumes) vols.push_back(json{{"id", r.id}, {"name", r.name}, {"kind", volume_kind_name(r.kind)},
        {"mount_point", r.mount_point}, {"mount_prefix", r.mount_prefix}, {"fs_types", r.fs_types}, {"subdir", r.subdir},
        {"writable", r.writable}, {"format_candidate", r.format_candidate}, {"protected_paths", r.protected_paths}});
    return json{{"bind_host", bind_host}, {"port", port}, {"tls_cert", tls_cert}, {"tls_key", tls_key},
                {"allow_plain_http", allow_plain_http}, {"data_dir", data_dir}, {"web_root", web_root},
                {"application_medium_mount", application_medium_mount}, {"volumes", vols},
                {"auth", {{"pairing_code", "(hidden)"}, {"admin_password_hash", auth.admin_password_hash.empty() ? "(not set)" : "(hidden)"},
                          {"idle_timeout_minutes", auth.idle_timeout_ms / 60000}, {"absolute_timeout_hours", auth.absolute_timeout_ms / 3600000},
                          {"max_failures", auth.max_failures}, {"lockout_seconds", auth.lockout_ms / 1000}, {"max_sessions", auth.max_sessions}}},
                {"max_upload_bytes", max_upload_bytes}, {"upload_chunk_max", upload_chunk_max}, {"workers", workers},
                {"max_queue", max_queue}, {"screen_max_fps", screen_max_fps}, {"screen_source", screen_source},
                {"format_fs", format_fs}, {"allowed_hosts", allowed_hosts}, {"index_on_start", index_on_start}, {"log_level", log_level}};
}

Status Config::load(const std::string& path, Config& out) {
    std::ifstream f(path);
    if (!f) return Status::fail(Err::not_found, "config file not found: " + path);
    json j = json::parse(f, nullptr, false);
    if (j.is_discarded()) return Status::fail(Err::invalid_argument, "config file is not valid JSON: " + path);
    try { out = from_json(j); } catch (const std::exception& e) { return Status::fail(Err::invalid_argument, std::string("config: ") + e.what()); }
    out.config_path = path;
    return Status::good();
}

Status Config::validate() const {
    if (data_dir.empty()) return Status::fail(Err::invalid_argument, "data_dir is required");
    if (port < 1 || port > 65535) return Status::fail(Err::invalid_argument, "port out of range");
    if (tls_cert.empty() != tls_key.empty()) return Status::fail(Err::invalid_argument, "tls_cert and tls_key must be set together");
    if (auth.pairing_code.size() < 6) return Status::fail(Err::invalid_argument, "auth.pairing_code must have at least 6 characters");
    if (upload_chunk_max < 65536 || upload_chunk_max > (64u << 20)) return Status::fail(Err::invalid_argument, "upload_chunk_max must be between 64 KiB and 64 MiB");
    if (screen_max_fps < 1 || screen_max_fps > 30) return Status::fail(Err::invalid_argument, "screen_max_fps must be 1..30");
    return Status::good();
}

std::string config_usage() {
    return
        "diskmode-webconsole [options]\n"
        "  --config <file.json>          load settings (see docs/CONFIG.md)\n"
        "  --data-dir <dir>              application data directory (required)\n"
        "  --web-root <dir>              static frontend directory\n"
        "  --bind <host>                 bind address (default 0.0.0.0)\n"
        "  --port <n>                    TCP port (default 8765)\n"
        "  --tls-cert <pem> --tls-key <pem>   enable HTTPS\n"
        "  --allow-plain-http            serve plain HTTP on non-loopback (sessions are then viewer-only)\n"
        "  --pairing-code <code>         pairing code (default: generated and logged)\n"
        "  --volumes-json <file>         volume policy rules (default: MPC Sample layout)\n"
        "  --screen-source <simulated|file:<path>|none>\n"
        "  --index-on-start              index volumes at startup\n"
        "  --log-level <debug|info|warn|error>\n"
        "  --hash-password <pw>          print a PBKDF2 hash for auth.admin_password_hash and exit\n"
        "  --print-config                print the effective configuration and exit\n"
        "  --self-test                   start, probe, stop; exit 0 when healthy\n"
        "  --help\n";
}

Status parse_args(int argc, char** argv, Config& cfg, bool& show_help, std::string& action, std::string& action_arg) {
    show_help = false;
    // first pass: --config
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], "--config") && i + 1 < argc) {
        Status st = Config::load(argv[i + 1], cfg); if (!st.ok()) return st;
    }
    auto need = [&](int& i) -> const char* { if (i + 1 >= argc) return nullptr; return argv[++i]; };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        const char* v = nullptr;
        if (a == "--help" || a == "-h") show_help = true;
        else if (a == "--config") { ++i; }
        else if (a == "--data-dir") { if (!(v = need(i))) return Status::fail(Err::invalid_argument, a + " needs a value"); cfg.data_dir = v; }
        else if (a == "--web-root") { if (!(v = need(i))) return Status::fail(Err::invalid_argument, a + " needs a value"); cfg.web_root = v; }
        else if (a == "--bind") { if (!(v = need(i))) return Status::fail(Err::invalid_argument, a + " needs a value"); cfg.bind_host = v; }
        else if (a == "--port") { if (!(v = need(i))) return Status::fail(Err::invalid_argument, a + " needs a value"); cfg.port = std::atoi(v); }
        else if (a == "--tls-cert") { if (!(v = need(i))) return Status::fail(Err::invalid_argument, a + " needs a value"); cfg.tls_cert = v; }
        else if (a == "--tls-key") { if (!(v = need(i))) return Status::fail(Err::invalid_argument, a + " needs a value"); cfg.tls_key = v; }
        else if (a == "--allow-plain-http") cfg.allow_plain_http = true;
        else if (a == "--pairing-code") { if (!(v = need(i))) return Status::fail(Err::invalid_argument, a + " needs a value"); cfg.auth.pairing_code = v; }
        else if (a == "--volumes-json") {
            if (!(v = need(i))) return Status::fail(Err::invalid_argument, a + " needs a value");
            std::ifstream f(v); json j = json::parse(f, nullptr, false);
            if (j.is_discarded()) return Status::fail(Err::invalid_argument, "volumes json invalid");
            try { cfg.volumes = VolumeRegistry::rules_from_json(j); } catch (const std::exception& e) { return Status::fail(Err::invalid_argument, e.what()); }
        }
        else if (a == "--screen-source") { if (!(v = need(i))) return Status::fail(Err::invalid_argument, a + " needs a value"); cfg.screen_source = v; }
        else if (a == "--index-on-start") cfg.index_on_start = true;
        else if (a == "--log-level") { if (!(v = need(i))) return Status::fail(Err::invalid_argument, a + " needs a value"); cfg.log_level = v; }
        else if (a == "--hash-password") { if (!(v = need(i))) return Status::fail(Err::invalid_argument, a + " needs a value"); action = "hash-password"; action_arg = v; }
        else if (a == "--print-config") action = "print-config";
        else if (a == "--self-test") action = "self-test";
        else return Status::fail(Err::invalid_argument, "unknown option: " + a);
    }
    if (cfg.volumes.empty()) cfg.volumes = VolumeRegistry::default_rules();
    return Status::good();
}

} // namespace dm
