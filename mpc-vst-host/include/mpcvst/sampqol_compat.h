// SPDX-License-Identifier: MIT
// The SampQOL compatibility adapter: the gate in front of any runtime
// integration with the stock MPC Sample app and SampQOL OS.
//
// STATUS: the gate is implemented; no audio or MIDI hook behind it is. No
// native interface for feeding plug-in audio into the stock voice path or
// for reading pad events has been verified for this project, so decide()
// always answers "disabled" with the reason, and nothing here touches the
// running app. See docs/SAMPQOL_INTEGRATION.md for what was found and what
// would have to be demonstrated first.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mpcvst {

// What the process this code runs in looks like.
struct HostEnvironment {
    std::string exe_path;             // /proc/self/exe
    std::string app_build_id;         // GNU build-id of the main program, hex
    bool sampqol_preloaded = false;   // mpcqol.so is in this process
    std::string ld_preload;
    bool disable_file_present = false;
    std::string sampqol_version;      // "" when unknown (SampQOL publishes no API for it)
};

// A stock app build this project knows about.
struct CompatEntry {
    const char* build_id;        // GNU build-id (hex) of /usr/bin/MPC Sample
    const char* firmware;        // the firmware it ships in
    bool identity_known;         // true: we know what this build is
    bool audio_hook_verified;    // true only once plug-in audio has been rendered through the sampler on hardware
    bool midi_hook_verified;     // true only once pad events have reached a plug-in on hardware
    const char* note;
};

const std::vector<CompatEntry>& compat_table();

struct CompatDecision {
    bool enabled = false;        // may the runtime adapter install anything?
    bool identity_known = false;
    std::string reason;          // one line for the log and the screen
    const CompatEntry* entry = nullptr;
};

// Reads the live environment of this process (Linux only; the fields that
// cannot be read stay empty).
HostEnvironment detect_environment(const std::string& disable_file_path);

// The gate. Fails closed: anything unknown or unverified means disabled.
CompatDecision decide(const HostEnvironment& env, bool user_enabled);

// Crash guard, kept in a tmpfs folder that resets at boot: how many times
// the app started with the adapter this boot and when; once the app stopped
// soon after such a start, the adapter stays off until the next boot.
struct CrashGuard {
    std::string state_dir;
    int max_starts = 3;
    long min_run_seconds = 600;
    // Records a start; false (with reason) when the guard says no.
    bool permit_start(long uptime_s, std::string& reason) const;
};

} // namespace mpcvst
