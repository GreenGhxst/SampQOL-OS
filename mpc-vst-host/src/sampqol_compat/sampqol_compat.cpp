// SPDX-License-Identifier: MIT
#include "mpcvst/sampqol_compat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
#include <elf.h>
#include <link.h>
#endif

namespace mpcvst {

const std::vector<CompatEntry>& compat_table() {
    // The one stock build that MPC-FM and Melodee both document for firmware
    // 1.3.0.12. Its identity is known; nothing about routing plug-in audio
    // through it has been verified by this project. Both flags stay false
    // until a hardware test says otherwise.
    static const std::vector<CompatEntry> table = {
        {"1dac7f481a0fe8d128e435ace88d7e6aece0c9c9", "1.3.0.12", true, false, false,
         "identity known from public MPC-FM notes; audio and pad hooks not verified by this project"},
    };
    return table;
}

#if defined(__linux__)
namespace {
struct PhdrState { std::string build_id; bool qol = false; int index = 0; };

int phdr_cb(struct dl_phdr_info* info, size_t, void* data) {
    PhdrState* st = static_cast<PhdrState*>(data);
    if (st->index++ == 0) {
        for (int i = 0; i < info->dlpi_phnum; ++i) {
            const ElfW(Phdr)& ph = info->dlpi_phdr[i];
            if (ph.p_type != PT_NOTE) continue;
            const unsigned char* p = reinterpret_cast<const unsigned char*>(info->dlpi_addr + ph.p_vaddr);
            const unsigned char* end = p + ph.p_memsz;
            while (end - p >= 12) {
                uint32_t nsz, dsz, type;
                std::memcpy(&nsz, p, 4); std::memcpy(&dsz, p + 4, 4); std::memcpy(&type, p + 8, 4);
                size_t name_room = (nsz + 3u) & ~3u, desc_room = (dsz + 3u) & ~3u;
                if (static_cast<size_t>(end - p) < 12 + name_room + dsz) break;
                const unsigned char* name = p + 12; const unsigned char* desc = name + name_room;
                if (type == NT_GNU_BUILD_ID && nsz == 4 && std::memcmp(name, "GNU", 4) == 0) {
                    char hex[3];
                    for (size_t k = 0; k < dsz && k < 32; ++k) { std::snprintf(hex, sizeof hex, "%02x", desc[k]); st->build_id += hex; }
                }
                if (static_cast<size_t>(end - desc) < desc_room) break;
                p = desc + desc_room;
            }
        }
    }
    if (info->dlpi_name && std::strstr(info->dlpi_name, "mpcqol.so")) st->qol = true;
    return 0;
}
} // namespace
#endif

HostEnvironment detect_environment(const std::string& disable_file_path) {
    HostEnvironment env;
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) { exe[n] = 0; env.exe_path = exe; }
    if (const char* p = std::getenv("LD_PRELOAD")) env.ld_preload = p;
#if defined(__linux__)
    PhdrState st;
    dl_iterate_phdr(phdr_cb, &st);
    env.app_build_id = st.build_id;
    env.sampqol_preloaded = st.qol || env.ld_preload.find("mpcqol.so") != std::string::npos;
#endif
    struct stat sb{};
    env.disable_file_present = !disable_file_path.empty() && stat(disable_file_path.c_str(), &sb) == 0;
    return env;
}

CompatDecision decide(const HostEnvironment& env, bool user_enabled) {
    CompatDecision d;
    for (const CompatEntry& e : compat_table())
        if (!env.app_build_id.empty() && env.app_build_id == e.build_id) { d.entry = &e; d.identity_known = e.identity_known; }
    if (env.disable_file_present) { d.reason = "disabled by the DISABLE file"; return d; }
    if (!user_enabled) { d.reason = "plug-in hosting is off in the settings"; return d; }
    if (env.exe_path != "/usr/bin/MPC Sample") { d.reason = "not inside the stock MPC Sample app (" + (env.exe_path.empty() ? std::string("unknown program") : env.exe_path) + ")"; return d; }
    if (!env.sampqol_preloaded) { d.reason = "SampQOL OS is not loaded in this app"; return d; }
    if (!d.entry) { d.reason = "unsupported app build " + (env.app_build_id.empty() ? std::string("(no build id)") : env.app_build_id); return d; }
    if (!d.entry->audio_hook_verified || !d.entry->midi_hook_verified) {
        d.reason = std::string("app build ") + d.entry->firmware + " is known, but no verified audio/MIDI integration exists for it yet";
        return d;
    }
    d.enabled = true;
    d.reason = std::string("verified integration for app build ") + d.entry->firmware;
    return d;
}

namespace {
long read_num(const std::string& path) {
    std::ifstream in(path);
    long v = -1;
    if (in) in >> v;
    return v;
}
void write_num(const std::string& path, long v) {
    std::ofstream out(path, std::ios::trunc);
    out << v << '\n';
}
} // namespace

bool CrashGuard::permit_start(long uptime_s, std::string& reason) const {
    if (state_dir.empty()) { reason = "no state folder for the crash guard"; return false; }
    struct stat sb{};
    if (stat((state_dir + "/off").c_str(), &sb) == 0) { reason = "off until the next boot (an earlier start ended too soon)"; return false; }
    long starts = read_num(state_dir + "/starts");
    long last = read_num(state_dir + "/last");
    if (starts < 0) starts = 0;
    if (starts >= max_starts) {
        write_num(state_dir + "/off", 1);
        reason = "the app already started " + std::to_string(max_starts) + " times with the expansion this boot";
        return false;
    }
    if (starts > 0 && last >= 0 && uptime_s >= 0 && uptime_s - last < min_run_seconds) {
        write_num(state_dir + "/off", 1);
        reason = "the app stopped less than " + std::to_string(min_run_seconds) + " s after its start with the expansion";
        return false;
    }
    write_num(state_dir + "/starts", starts + 1);
    write_num(state_dir + "/last", uptime_s);
    reason = "start " + std::to_string(starts + 1) + " of " + std::to_string(max_starts);
    return true;
}

} // namespace mpcvst
