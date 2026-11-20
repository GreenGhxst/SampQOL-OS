// DISKMODE Web Console entry point.
#include <csignal>
#include <cstdio>
#include <iostream>
#include <unistd.h>
#include "server/web_console.h"
#include "core/network.h"

namespace {
volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }
}

int main(int argc, char** argv) {
    using namespace dm;
    Config cfg; bool help = false; std::string action, action_arg;
    Status st = parse_args(argc, argv, cfg, help, action, action_arg);
    if (!st.ok()) { std::fprintf(stderr, "error: %s\n\n%s", st.message.c_str(), config_usage().c_str()); return 2; }
    if (help) { std::fputs(config_usage().c_str(), stdout); return 0; }
    if (action == "hash-password") { std::puts(make_password_hash(action_arg).c_str()); return 0; }
    log_set_level(cfg.log_level == "debug" ? LogLevel::debug : cfg.log_level == "warn" ? LogLevel::warn : cfg.log_level == "error" ? LogLevel::error : LogLevel::info);
    if (cfg.auth.pairing_code.empty()) cfg.auth.pairing_code = random_token(4);
    if (action == "print-config") { std::puts(cfg.to_json().dump(2).c_str()); return 0; }
    st = cfg.validate();
    if (!st.ok()) { std::fprintf(stderr, "config error: %s\n", st.message.c_str()); return 2; }

    std::unique_ptr<FrameSource> screen;
    if (cfg.screen_source == "simulated") screen = make_simulated_frame_source();
    else if (cfg.screen_source.rfind("file:", 0) == 0) screen = make_file_frame_source(cfg.screen_source.substr(5));
    else screen = make_unavailable_native_source("native capture is not available under the stock MPC Sample application (DRM dumb buffer without readback)");

    WebConsole console(cfg, make_linux_probe(cfg.application_medium_mount), make_linux_block_inventory(), std::move(screen));
    if (!console.start()) return 1;
    log_info("pairing code: " + console.auth().pairing_code());
    for (const auto& u : network_status_json(cfg.bind_host, console.port(), console.tls())["urls"]) log_info("reachable at " + u.get<std::string>());

    if (action == "self-test") {
        bool ok = console.port() > 0;
        console.stop();
        std::puts(ok ? "self-test ok" : "self-test failed");
        return ok ? 0 : 1;
    }
    std::signal(SIGINT, on_signal); std::signal(SIGTERM, on_signal);
    while (!g_stop) ::usleep(200000);
    console.stop();
    return 0;
}
