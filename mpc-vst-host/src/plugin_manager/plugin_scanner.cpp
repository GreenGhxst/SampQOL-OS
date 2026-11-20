// SPDX-License-Identifier: MIT
#include "mpcvst/plugin_scanner.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace mpcvst {

namespace {

int64_t now_s() {
    struct timespec t{};
    clock_gettime(CLOCK_REALTIME, &t);
    return t.tv_sec;
}

int64_t now_ms() {
    struct timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<int64_t>(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}

bool ends_with(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

void split_words(const std::string& text, std::vector<std::string>& out) {
    std::string word;
    for (char c : text) {
        if (c == ' ' || c == '\t') { if (!word.empty()) out.push_back(word); word.clear(); }
        else word.push_back(c);
    }
    if (!word.empty()) out.push_back(word);
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\r' || s[a] == '\n' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\r' || s[b - 1] == '\n' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

} // namespace

// ---- the worker report ----

bool parse_worker_report(const std::string& text, WorkerResult& r) {
    bool saw_result = false;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        auto num = [&]() { return static_cast<int32_t>(std::strtol(value.c_str(), nullptr, 10)); };
        if (key == "result") {
            saw_result = true;
            if (value == "ok") r.outcome = WorkerResult::Outcome::Ok;
            else if (value == "rejected") r.outcome = WorkerResult::Outcome::Rejected;
            else r.outcome = WorkerResult::Outcome::Failed;
        } else if (key == "message") r.message = value;
        else if (key == "name") r.info.name = value;
        else if (key == "vendor") r.info.vendor = value;
        else if (key == "product") r.info.product = value;
        else if (key == "unique_id") r.info.unique_id = num();
        else if (key == "version") r.info.version = num();
        else if (key == "vst_version") r.info.vst_version = num();
        else if (key == "num_params") r.info.num_params = num();
        else if (key == "num_programs") r.info.num_programs = num();
        else if (key == "num_inputs") r.info.num_inputs = num();
        else if (key == "num_outputs") r.info.num_outputs = num();
        else if (key == "initial_delay") r.info.initial_delay = num();
        else if (key == "flags") r.info.flags = num();
        else if (key == "category") r.info.category = num();
        else if (key == "kind") r.info.kind = value == "instrument" ? PluginKind::Instrument : value == "effect" ? PluginKind::Effect : PluginKind::Unknown;
        else if (key == "is_synth") r.info.is_synth = value == "1";
        else if (key == "has_chunks") r.info.has_chunks = value == "1";
        else if (key == "can_replace") r.info.can_replace = value == "1";
        else if (key == "receives_midi") r.info.receives_midi = value == "1";
        else if (key == "has_editor") r.info.has_editor = value == "1";
        else if (key == "process_block_ms") r.process_block_ms = std::strtod(value.c_str(), nullptr);
    }
    return saw_result;
}

WorkerResult probe_in_worker(const ScanPolicy& policy, const std::string& library_path) {
    WorkerResult result;
    if (policy.worker_exe.empty()) {
        result.message = "no worker configured";
        return result;
    }
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) != 0) {
        result.message = std::string("pipe failed: ") + std::strerror(errno);
        return result;
    }
    pid_t pid = fork();
    if (pid < 0) {
        result.message = std::string("fork failed: ") + std::strerror(errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }
    if (pid == 0) {
        // Child: limits first, then the worker. Its stderr is kept (logs).
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        struct rlimit mem{policy.worker_memory_limit, policy.worker_memory_limit};
        setrlimit(RLIMIT_AS, &mem);
        struct rlimit cpu{static_cast<rlim_t>(policy.worker_cpu_seconds), static_cast<rlim_t>(policy.worker_cpu_seconds + 2)};
        setrlimit(RLIMIT_CPU, &cpu);
        struct rlimit core{0, 0};
        setrlimit(RLIMIT_CORE, &core);
        std::string rate = std::to_string(policy.probe_sample_rate);
        std::string block = std::to_string(policy.probe_block_size);
        std::vector<std::string> words = policy.worker_launcher;
        if (words.empty())
            if (const char* env = std::getenv("MPCVST_WORKER_LAUNCHER")) split_words(env, words);
        words.push_back(policy.worker_exe);
        for (const char* w : {"--rate", rate.c_str(), "--block", block.c_str(), "--"}) words.push_back(w);
        words.push_back(library_path);
        std::vector<char*> argv;
        for (std::string& w : words) argv.push_back(&w[0]);
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(126);
    }
    close(pipefd[1]);

    std::string output;
    const int64_t deadline = now_ms() + policy.worker_timeout_ms;
    bool timed_out = false;
    char buf[4096];
    while (true) {
        int64_t left = deadline - now_ms();
        if (left <= 0) { timed_out = true; break; }
        struct pollfd pfd{pipefd[0], POLLIN, 0};
        int pr = poll(&pfd, 1, static_cast<int>(std::min<int64_t>(left, 1000)));
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue;
        ssize_t n = read(pipefd[0], buf, sizeof buf);
        if (n <= 0) break; // EOF: the worker exited or closed stdout
        if (output.size() < (1u << 20)) output.append(buf, static_cast<size_t>(n));
    }
    close(pipefd[0]);

    int status = 0;
    if (timed_out) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        result.outcome = WorkerResult::Outcome::TimedOut;
        result.message = "worker did not finish within " + std::to_string(policy.worker_timeout_ms) + " ms (killed)";
        return result;
    }
    // The pipe closed; give the worker a moment to exit, then reap.
    const int64_t reap_deadline = now_ms() + 2000;
    pid_t w;
    while ((w = waitpid(pid, &status, WNOHANG)) == 0 && now_ms() < reap_deadline) usleep(2000);
    if (w == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    if (WIFSIGNALED(status)) {
        result.outcome = WorkerResult::Outcome::Crashed;
        result.signal = WTERMSIG(status);
        result.message = std::string("worker killed by signal ") + std::to_string(result.signal) + " (" + strsignal(result.signal) + ")";
        return result;
    }
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (result.exit_code == 126) {
        result.message = "worker executable could not be started: " + policy.worker_exe;
        return result;
    }
    if (!parse_worker_report(output, result)) {
        result.outcome = WorkerResult::Outcome::Failed;
        result.message = "worker gave no report (exit " + std::to_string(result.exit_code) + ")";
        return result;
    }
    if (result.outcome == WorkerResult::Outcome::Ok && result.exit_code != 0) {
        result.outcome = WorkerResult::Outcome::Failed;
        result.message = "worker reported ok but exited " + std::to_string(result.exit_code);
    }
    result.info.path = library_path;
    return result;
}

// ---- the scanner ----

void PluginScanner::walk(const std::string& dir, int depth, std::vector<std::string>& out) const {
    if (depth > policy_.max_depth || static_cast<int>(out.size()) >= policy_.max_files) return;
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> subdirs;
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.empty() || name[0] == '.' || name == "System Volume Information") continue;
        std::string path = dir + "/" + name;
        struct stat st{};
        if ((policy_.follow_symlinks ? stat : lstat)(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) subdirs.push_back(path);
        else if (S_ISREG(st.st_mode) && ends_with(name, ".so")) {
            if (static_cast<int>(out.size()) >= policy_.max_files) break;
            out.push_back(path);
        }
    }
    closedir(d);
    std::sort(subdirs.begin(), subdirs.end());
    for (const std::string& s : subdirs) walk(s, depth + 1, out);
}

std::vector<std::string> PluginScanner::candidates() const {
    std::vector<std::string> out;
    for (const std::string& dir : policy_.plugin_dirs) walk(dir, 0, out);
    std::sort(out.begin(), out.end());
    return out;
}

std::string PluginScanner::validate_static(const std::string& path, ElfInfo& elf) const {
    if (!inspect_elf(path, elf)) return "unreadable: " + elf.error;
    if (!elf.is_elf) return "not an ELF file";
    if (!elf.little_endian) return "big-endian ELF";
    if (!elf.shared_object) return "not a shared object (ET_DYN)";
    const ElfMachine host = host_machine();
    if (elf.machine != host) {
        std::string why = std::string("built for ") + elf_machine_name(elf.machine) + ", this device runs " + elf_machine_name(host);
        if (elf.machine == ElfMachine::Arm32) why += " (32-bit ARM plug-ins need the bridge, which is not available yet)";
        return why;
    }
    if (host == ElfMachine::AArch64 && !elf.class64) return "32-bit ELF class on a 64-bit device";
    if (!elf.exports_vst_entry) return "no VST2 entry point exported (VSTPluginMain or main)";
    std::vector<std::string> missing = missing_dependencies(elf, policy_.library_dirs);
    if (!missing.empty()) {
        std::string why = "missing librar" + std::string(missing.size() == 1 ? "y: " : "ies: ");
        for (size_t i = 0; i < missing.size(); ++i) why += (i ? ", " : "") + missing[i];
        return why;
    }
    return {};
}

ScanReport PluginScanner::scan(PluginRegistry& registry, const std::function<void(const std::string&)>& progress) const {
    ScanReport report;
    std::vector<std::string> files = candidates();
    report.candidates = static_cast<int>(files.size());

    // Forget records whose files are gone (but keep quarantine entries:
    // the plug-in may come back on the next card).
    std::vector<std::string> gone;
    for (const PluginRecord& r : registry.records()) {
        struct stat st{};
        bool in_dirs = std::find(files.begin(), files.end(), r.path) != files.end();
        if (!in_dirs && stat(r.path.c_str(), &st) != 0 && r.status != PluginStatus::Quarantined) gone.push_back(r.path);
    }
    for (const std::string& p : gone) registry.remove(p);

    for (const std::string& path : files) {
        if (progress) progress(path);
        struct stat st{};
        if (stat(path.c_str(), &st) != 0) continue;
        const uint64_t size = static_cast<uint64_t>(st.st_size);
        const int64_t mtime = static_cast<int64_t>(st.st_mtime);

        if (const PluginRecord* cached = registry.find(path)) {
            if (cached->same_file(size, mtime) && !policy_.rescan_unchanged) {
                ++report.skipped_cached;
                continue; // quarantined, rejected or ok: unchanged files keep their verdict
            }
        }
        PluginRecord record;
        record.path = path;
        record.file_size = size;
        record.mtime = mtime;
        record.last_scan = now_s();
        record.info.path = path;

        ElfInfo elf;
        std::string why = validate_static(path, elf);
        if (!why.empty()) {
            record.status = PluginStatus::Rejected;
            record.reason = why;
            registry.put(record);
            ++report.rejected;
            continue;
        }
        if (policy_.worker_exe.empty()) {
            // Static checks only: list it, but it is not validated to run.
            record.status = PluginStatus::Unknown;
            record.reason = "not probed (no worker)";
            registry.put(record);
            continue;
        }
        ++report.probed;
        WorkerResult w = probe_in_worker(policy_, path);
        switch (w.outcome) {
        case WorkerResult::Outcome::Ok:
            record.status = PluginStatus::Ok;
            record.info = w.info;
            record.info.path = path;
            record.reason.clear();
            ++report.ok;
            break;
        case WorkerResult::Outcome::Rejected:
            record.status = PluginStatus::Rejected;
            record.reason = w.message;
            ++report.rejected;
            break;
        case WorkerResult::Outcome::Crashed:
        case WorkerResult::Outcome::TimedOut:
            record.status = PluginStatus::Quarantined;
            record.reason = w.message;
            record.crash_count = 1;
            if (const PluginRecord* old = registry.find(path)) record.crash_count = old->crash_count + 1;
            ++report.quarantined;
            break;
        case WorkerResult::Outcome::Failed:
            record.status = PluginStatus::Rejected;
            record.reason = "probe failed: " + w.message;
            ++report.rejected;
            report.notes.push_back(path + ": " + w.message);
            break;
        }
        registry.put(record);
    }
    return report;
}

} // namespace mpcvst
