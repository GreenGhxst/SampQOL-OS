// SPDX-License-Identifier: MIT
// Tests 13-15 of the specification: an unsupported SampQOL/app version is
// refused, recovery from a failed start (the crash guard), and the host
// disabled leaves ordinary start-up alone (the gate stays closed and
// nothing is installed). The gate is pure logic, so this runs on any machine.
#include "check.h"
#include "test_paths.h"

#include "mpcvst/sampqol_compat.h"

#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace mpcvst;

static void rm(const std::string& p) { unlink(p.c_str()); }

int main() {
    // The live environment of this test process: not the stock app.
    HostEnvironment here = detect_environment("");
    CHECK(!here.exe_path.empty());
    CHECK(!here.sampqol_preloaded);
    CompatDecision d = decide(here, true);
    CHECK(!d.enabled);
    CHECK_MSG(d.reason.find("not inside the stock") != std::string::npos, d.reason);

    // 13. unsupported SampQOL / app version: everything right except the build id.
    HostEnvironment env;
    env.exe_path = "/usr/bin/MPC Sample";
    env.sampqol_preloaded = true;
    env.app_build_id = "deadbeef00000000000000000000000000000000";
    d = decide(env, true);
    CHECK(!d.enabled && !d.identity_known);
    CHECK_MSG(d.reason.find("unsupported app build") != std::string::npos, d.reason);
    env.app_build_id.clear();
    d = decide(env, true);
    CHECK(!d.enabled);
    CHECK_MSG(d.reason.find("no build id") != std::string::npos, d.reason);

    // The one known build: identity known, but no verified hook: still off.
    env.app_build_id = "1dac7f481a0fe8d128e435ace88d7e6aece0c9c9";
    d = decide(env, true);
    CHECK(d.identity_known);
    CHECK(!d.enabled);
    CHECK_MSG(d.reason.find("no verified audio/MIDI integration") != std::string::npos, d.reason);
    CHECK(d.entry && !d.entry->audio_hook_verified && !d.entry->midi_hook_verified);

    // Without SampQOL in the process: off, with the reason.
    HostEnvironment stock = env;
    stock.sampqol_preloaded = false;
    d = decide(stock, true);
    CHECK(!d.enabled && d.reason.find("SampQOL OS is not loaded") != std::string::npos);

    // 15. host disabled by the user or the DISABLE file: off before anything else is looked at.
    d = decide(env, false);
    CHECK(!d.enabled && d.reason.find("off in the settings") != std::string::npos);
    HostEnvironment disabled = env;
    disabled.disable_file_present = true;
    d = decide(disabled, true);
    CHECK(!d.enabled && d.reason.find("DISABLE file") != std::string::npos);
    // detect_environment sees a DISABLE file.
    mkdir(temp_dir().c_str(), 0755);
    std::string disable_path = temp_dir() + "/DISABLE";
    { std::ofstream f(disable_path); f << "off\n"; }
    CHECK(detect_environment(disable_path).disable_file_present);
    rm(disable_path);
    CHECK(!detect_environment(disable_path).disable_file_present);

    // The table never contains a verified entry without hardware evidence.
    for (const CompatEntry& e : compat_table()) {
        CHECK(!e.audio_hook_verified);
        CHECK(!e.midi_hook_verified);
    }

    // 14. recovery from a failed start-up: the crash guard.
    std::string state = temp_dir() + "/guard";
    mkdir(state.c_str(), 0755);
    rm(state + "/starts"); rm(state + "/last"); rm(state + "/off");
    CrashGuard guard;
    guard.state_dir = state;
    guard.max_starts = 3;
    guard.min_run_seconds = 600;
    std::string why;
    CHECK(guard.permit_start(100, why));           // first start
    CHECK(!guard.permit_start(200, why));          // restarted 100 s later: a crash; off
    CHECK_MSG(why.find("stopped less than") != std::string::npos, why);
    CHECK(!guard.permit_start(5000, why));         // stays off this boot
    CHECK_MSG(why.find("until the next boot") != std::string::npos, why);
    // A new boot (fresh tmpfs): long runs are fine up to max_starts.
    rm(state + "/starts"); rm(state + "/last"); rm(state + "/off");
    CHECK(guard.permit_start(100, why));
    CHECK(guard.permit_start(1000, why));
    CHECK(guard.permit_start(2000, why));
    CHECK(!guard.permit_start(3000, why));
    CHECK_MSG(why.find("already started") != std::string::npos, why);
    CrashGuard nostate;
    CHECK(!nostate.permit_start(1, why));

    return check::summary("test_compat");
}
