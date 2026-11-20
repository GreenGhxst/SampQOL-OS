// Server configuration: JSON file plus command-line overrides.
#pragma once
#include <string>
#include <vector>
#include "../core/volume_registry.h"
#include "../util/common.h"
#include "auth.h"

namespace dm {

struct Config {
    std::string bind_host = "0.0.0.0";
    int port = 8765;                       // proposed DISKMODE port (stock firmware uses 80)
    std::string tls_cert, tls_key;         // PEM; TLS is on when both are set
    bool allow_plain_http = false;         // without TLS, bind loopback only unless set
    std::string data_dir;                  // DISKMODE application data (journal, index, staging, trash index)
    std::string web_root;                  // static frontend files
    std::string application_medium_mount;  // mount point hosting the autorun folder, if known
    std::vector<VolumeRule> volumes;
    AuthConfig auth;
    std::uint64_t max_upload_bytes = 4ull << 30;
    std::size_t upload_chunk_max = 8u << 20;
    int workers = 2;
    std::size_t max_queue = 64;
    int screen_max_fps = 10;
    std::string screen_source = "simulated";   // simulated | file:<path> | none
    std::vector<std::string> format_fs = {"exfat"};
    std::vector<std::string> allowed_hosts;    // extra Host header values (besides IP literals, localhost, *.local)
    bool index_on_start = false;
    std::string log_level = "info";
    std::string config_path;

    static Config from_json(const json& j);
    json to_json() const;
    static Status load(const std::string& path, Config& out);
    Status validate() const;
};

// Usage text for the binary.
std::string config_usage();
// Parses argv, loading --config first and then applying overrides.
Status parse_args(int argc, char** argv, Config& cfg, bool& show_help, std::string& one_shot_action, std::string& action_arg);

} // namespace dm
