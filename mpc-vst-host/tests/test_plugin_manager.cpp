// SPDX-License-Identifier: MIT
// Tests 1-4 and 12 of the specification: valid discovery, invalid ELF
// rejection, wrong architecture, missing dependency, and crashing (and
// hanging) plug-in behaviour through the worker. Also the registry cache.
#include "check.h"
#include "test_paths.h"

#include "mpcvst/elf_inspect.h"
#include "mpcvst/plugin_registry.h"
#include "mpcvst/plugin_scanner.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace mpcvst;

static void write_file(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// A minimal 64-byte ELF64 header: enough for the inspector to read class,
// machine and type; no sections (so no entry point either).
static std::string fake_elf64(uint16_t machine, uint16_t type) {
    std::string h(64, '\0');
    h[0] = 0x7f; h[1] = 'E'; h[2] = 'L'; h[3] = 'F'; h[4] = 2; h[5] = 1; h[6] = 1;
    h[16] = static_cast<char>(type & 0xff); h[17] = static_cast<char>(type >> 8);
    h[18] = static_cast<char>(machine & 0xff); h[19] = static_cast<char>(machine >> 8);
    h[52] = 64; // e_ehsize
    return h;
}

static const PluginRecord* find(const PluginRegistry& reg, const char* name) {
    for (const PluginRecord& r : reg.records())
        if (r.path.size() >= std::strlen(name) && r.path.compare(r.path.size() - std::strlen(name), std::strlen(name), name) == 0) return &r;
    return nullptr;
}

int main() {
    const std::string tmp = temp_dir() + "/scan";
    mkdir(temp_dir().c_str(), 0755);
    mkdir(tmp.c_str(), 0755);
    mkdir((tmp + "/Instruments").c_str(), 0755);
    mkdir((tmp + "/Effects").c_str(), 0755);
    mkdir((tmp + "/Effects/deeper").c_str(), 0755);
    mkdir((tmp + "/outside").c_str(), 0755); // not an approved folder

    auto link = [&](const char* name, const std::string& into) {
        std::string dst = into + "/" + name;
        unlink(dst.c_str());
        // Copies, not symlinks: the scanner does not follow symlinks.
        std::ifstream in(plugin_path(name), std::ios::binary);
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        out << in.rdbuf();
        chmod(dst.c_str(), 0644);
    };
    link("mock_instrument.so", tmp + "/Instruments");
    link("mock_effect.so", tmp + "/Effects/deeper");
    link("bad_no_entry.so", tmp + "/Effects");
    link("bad_wrong_magic.so", tmp + "/Instruments");
    link("bad_crash_open.so", tmp + "/Instruments");
    link("bad_hang.so", tmp + "/Instruments");
    link("bad_crash_process.so", tmp + "/Instruments");
    link("bad_huge_io.so", tmp + "/Instruments");
    link("bad_missing_dep.so", tmp + "/Instruments");
    link("mock_instrument.so", tmp + "/outside");
    write_file(tmp + "/Instruments/not_elf.so", "this is not an ELF file at all, just text\n");
    write_file(tmp + "/Instruments/tiny.so", "MZ");
    // Wrong architecture: the other machine's header. The scanner on x86_64
    // sees an aarch64 file and vice versa.
    const uint16_t other = host_machine() == ElfMachine::AArch64 ? 62 : 183;
    write_file(tmp + "/Instruments/wrong_arch.so", fake_elf64(other, 3));
    write_file(tmp + "/Instruments/arm32.so", fake_elf64(40, 3));
    write_file(tmp + "/Instruments/executable.so", fake_elf64(static_cast<uint16_t>(host_machine()), 2));
    (void)!symlink(plugin_path("mock_effect.so").c_str(), (tmp + "/Effects/linked.so").c_str());

    // --- the ELF inspector on its own
    ElfInfo elf;
    REQUIRE(inspect_elf(plugin_path("mock_instrument.so"), elf));
    CHECK(elf.is_elf && elf.shared_object && elf.little_endian);
    CHECK(elf.machine == host_machine());
    CHECK(elf.exports_vst_entry && elf.entry_symbol == "VSTPluginMain");
    CHECK(!elf.needed.empty()); // libc, libm
    CHECK(missing_dependencies(elf, {}).empty());
    REQUIRE(inspect_elf(plugin_path("bad_no_entry.so"), elf));
    CHECK(elf.is_elf && !elf.exports_vst_entry);
    REQUIRE(inspect_elf(plugin_path("bad_missing_dep.so"), elf));
    bool needs_fake = false;
    for (const std::string& n : elf.needed) needs_fake = needs_fake || n.find("fakedep") != std::string::npos;
    CHECK(needs_fake);
    std::vector<std::string> missing = missing_dependencies(elf, {});
    CHECK(missing.size() == 1 && missing[0].find("fakedep") != std::string::npos);
    REQUIRE(inspect_elf(tmp + "/Instruments/not_elf.so", elf));
    CHECK(!elf.is_elf);
    CHECK(!inspect_elf(tmp + "/Instruments/missing.so", elf)); // unreadable
    REQUIRE(inspect_elf(tmp + "/Instruments/wrong_arch.so", elf));
    CHECK(elf.is_elf && elf.machine != host_machine());

    // --- the scanner: static validation and the worker
    ScanPolicy policy;
    policy.plugin_dirs = {tmp + "/Instruments", tmp + "/Effects"};
    policy.worker_exe = tool_path("mpcvst-probe");
    policy.worker_timeout_ms = 3000;
    PluginScanner scanner(policy);
    std::vector<std::string> files = scanner.candidates();
    CHECK_MSG(files.size() == 14, "candidates: " + std::to_string(files.size())); // 12 in Instruments, 2 in Effects; not the symlink, not the outside folder
    bool outside_listed = false, symlink_listed = false;
    for (const std::string& f : files) {
        outside_listed = outside_listed || f.find("/outside/") != std::string::npos;
        symlink_listed = symlink_listed || f.find("linked.so") != std::string::npos;
    }
    CHECK_MSG(!outside_listed, "only approved folders are read");
    CHECK_MSG(!symlink_listed, "symlinks are not followed");

    PluginRegistry registry;
    ScanReport report = scanner.scan(registry);
    CHECK(report.candidates == 14);
    // 1. valid discovery
    const PluginRecord* inst = find(registry, "Instruments/mock_instrument.so");
    REQUIRE(inst != nullptr);
    CHECK_MSG(inst->status == PluginStatus::Ok, inst->reason);
    CHECK(inst->info.name == "Mock Instrument" && inst->info.kind == PluginKind::Instrument && inst->info.num_params == 4);
    const PluginRecord* fx = find(registry, "deeper/mock_effect.so");
    REQUIRE(fx != nullptr);
    CHECK_MSG(fx->status == PluginStatus::Ok, fx->reason);
    CHECK(fx->info.kind == PluginKind::Effect);
    CHECK(report.ok == 2);
    // 2. invalid ELF rejection
    const PluginRecord* r = find(registry, "not_elf.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Rejected); CHECK_MSG(r->reason.find("not an ELF") != std::string::npos, r->reason);
    r = find(registry, "tiny.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Rejected);
    r = find(registry, "executable.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Rejected); CHECK_MSG(r->reason.find("shared object") != std::string::npos, r->reason);
    r = find(registry, "bad_no_entry.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Rejected); CHECK_MSG(r->reason.find("entry point") != std::string::npos, r->reason);
    // 3. wrong architecture
    r = find(registry, "wrong_arch.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Rejected); CHECK_MSG(r->reason.find("built for") != std::string::npos, r->reason);
    r = find(registry, "arm32.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Rejected); CHECK_MSG(r->reason.find("bridge") != std::string::npos, r->reason);
    // 4. missing dependency
    r = find(registry, "bad_missing_dep.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Rejected); CHECK_MSG(r->reason.find("missing librar") != std::string::npos, r->reason);
    // metadata rejections from the worker
    r = find(registry, "bad_wrong_magic.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Rejected); CHECK_MSG(r->reason.find("magic") != std::string::npos, r->reason);
    r = find(registry, "bad_huge_io.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Rejected);
    // 12. crashing plug-in behaviour: quarantined, the scanner survives
    r = find(registry, "bad_crash_open.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Quarantined); CHECK_MSG(r->reason.find("signal") != std::string::npos, r->reason); CHECK(r->crash_count == 1);
    r = find(registry, "bad_crash_process.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Quarantined); CHECK_MSG(r->reason.find("signal") != std::string::npos, r->reason);
    r = find(registry, "bad_hang.so");
    REQUIRE(r); CHECK(r->status == PluginStatus::Quarantined); CHECK_MSG(r->reason.find("did not finish") != std::string::npos, r->reason);
    CHECK(report.quarantined == 3);
    CHECK(registry.quarantined(tmp + "/Instruments/bad_hang.so"));
    CHECK(registry.loadable().size() == 2);
    CHECK(registry.loadable(PluginKind::Instrument).size() == 1);

    // --- the cache: a second scan probes nothing; a changed file is probed again
    const std::string cache = tmp + "/registry.txt";
    std::string error;
    REQUIRE(registry.save(cache, error));
    PluginRegistry again;
    REQUIRE(again.load(cache, error));
    CHECK(again.records().size() == registry.records().size());
    const PluginRecord* cached = find(again, "Instruments/mock_instrument.so");
    REQUIRE(cached);
    CHECK(cached->status == PluginStatus::Ok && cached->info.name == "Mock Instrument" && cached->info.unique_id == inst->info.unique_id);
    ScanReport second = scanner.scan(again);
    CHECK_MSG(second.probed == 0 && second.skipped_cached == 14, "probed " + std::to_string(second.probed) + " cached " + std::to_string(second.skipped_cached));
    // Quarantine clears, then the plug-in is probed again (and quarantined again).
    again.clear_quarantine(tmp + "/Instruments/bad_crash_open.so");
    ScanReport third = scanner.scan(again);
    CHECK(third.probed == 1);
    const PluginRecord* crash = find(again, "bad_crash_open.so");
    REQUIRE(crash);
    CHECK(crash->status == PluginStatus::Quarantined && crash->crash_count == 2);
    // A file that changed (touch its mtime into the future) is rescanned.
    {
        struct stat st{};
        std::string p = tmp + "/Effects/deeper/mock_effect.so";
        stat(p.c_str(), &st);
        struct timespec times[2] = {{st.st_atime + 100, 0}, {st.st_mtime + 100, 0}};
        utimensat(AT_FDCWD, p.c_str(), times, 0);
        ScanReport fourth = scanner.scan(again);
        CHECK(fourth.probed == 1 && fourth.ok == 1);
    }
    // A record whose file disappeared is dropped; a quarantine survives.
    unlink((tmp + "/Effects/deeper/mock_effect.so").c_str());
    unlink((tmp + "/Instruments/bad_hang.so").c_str());
    scanner.scan(again);
    CHECK(find(again, "deeper/mock_effect.so") == nullptr);
    CHECK(find(again, "bad_hang.so") != nullptr);

    // --- the worker report parser
    WorkerResult w;
    CHECK(parse_worker_report("name=X\nnum_params=3\nkind=effect\nresult=ok\nmessage=fine\n", w));
    CHECK(w.outcome == WorkerResult::Outcome::Ok && w.info.num_params == 3 && w.info.kind == PluginKind::Effect && w.message == "fine");
    WorkerResult none;
    CHECK(!parse_worker_report("garbage\n", none));

    // --- no worker configured: static checks only, nothing probed
    ScanPolicy no_worker = policy;
    no_worker.worker_exe.clear();
    PluginRegistry statics;
    ScanReport sr = PluginScanner(no_worker).scan(statics);
    CHECK(sr.probed == 0);
    const PluginRecord* unprobed = find(statics, "Instruments/mock_instrument.so");
    REQUIRE(unprobed);
    CHECK(unprobed->status == PluginStatus::Unknown);
    CHECK(statics.loadable().empty()); // not validated: not offered

    return check::summary("test_plugin_manager");
}
