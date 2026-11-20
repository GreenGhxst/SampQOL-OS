// SPDX-License-Identifier: MIT
// The plug-in scanner: walks only the approved folders, inspects each
// candidate without running it, then has a separate worker process open
// the plausible ones under resource and time limits. A crash or hang in
// the worker becomes a quarantine entry, never a dead host.
#pragma once

#include "mpcvst/elf_inspect.h"
#include "mpcvst/plugin_registry.h"

#include <functional>
#include <string>
#include <vector>

namespace mpcvst {

struct ScanPolicy {
    std::vector<std::string> plugin_dirs;      // the only folders ever read
    std::vector<std::string> library_dirs;     // extra folders for a plug-in's dependencies
    std::string worker_exe;                     // mpcvst-probe; empty: inspect only, no worker
    // Words put in front of the worker command line. Empty on the device.
    // Tests under QEMU user emulation set it (from MPCVST_WORKER_LAUNCHER)
    // because an emulated process cannot exec another aarch64 binary itself.
    std::vector<std::string> worker_launcher;
    int max_depth = 3;                          // folders below a plug-in dir
    int max_files = 2000;                       // candidates per scan
    int worker_timeout_ms = 10000;
    uint64_t worker_memory_limit = 512ull << 20;
    int worker_cpu_seconds = 20;
    bool follow_symlinks = false;
    bool rescan_unchanged = false;              // probe again even if size and mtime match
    double probe_sample_rate = 44100.0;
    int probe_block_size = 128;
};

struct WorkerResult {
    enum class Outcome { Ok, Rejected, Crashed, TimedOut, Failed } outcome = Outcome::Failed;
    int exit_code = 0;
    int signal = 0;
    std::string message;
    PluginInfo info;
    double process_block_ms = 0.0;   // the worker's time for one block of silence
};

// Runs the worker on one library. Blocks for at most policy.worker_timeout_ms.
WorkerResult probe_in_worker(const ScanPolicy& policy, const std::string& library_path);

// Parses the worker's stdout (key=value lines). Exposed for the tests.
bool parse_worker_report(const std::string& text, WorkerResult& result);

struct ScanReport {
    int candidates = 0;
    int probed = 0;
    int ok = 0;
    int rejected = 0;
    int quarantined = 0;
    int skipped_cached = 0;
    std::vector<std::string> notes;
};

class PluginScanner {
public:
    explicit PluginScanner(ScanPolicy policy) : policy_(std::move(policy)) {}

    // Lists the candidate files (regular files, inside the approved folders).
    std::vector<std::string> candidates() const;

    // Static validation only: ELF, machine, type, dependencies, entry point.
    // Returns an empty string when the file may be handed to the worker.
    std::string validate_static(const std::string& path, ElfInfo& elf) const;

    // Full scan into the registry. Quarantined entries are skipped unless
    // the file changed. `progress` (optional) is told each candidate.
    ScanReport scan(PluginRegistry& registry, const std::function<void(const std::string&)>& progress = {}) const;

    const ScanPolicy& policy() const { return policy_; }

private:
    void walk(const std::string& dir, int depth, std::vector<std::string>& out) const;
    ScanPolicy policy_;
};

} // namespace mpcvst
