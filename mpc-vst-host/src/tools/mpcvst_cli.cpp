// SPDX-License-Identifier: MIT
// mpcvst: the command line of the standalone host.
//   mpcvst scan  [--worker PATH] [--cache FILE] [--lib DIR]... DIR...
//   mpcvst info  PLUGIN.so
//   mpcvst render PLUGIN.so [--effect FX.so]... [--midi SCRIPT] [--seconds S] [--rate HZ]
//                 [--block N] [--tempo BPM] [--program N] [--param I=V]... [--input IN.wav]
//                 [--state FILE] [--save-state FILE] [--out OUT.wav] [--float]
//   mpcvst bench PLUGIN.so [--instances N] [--seconds S] [--rate HZ] [--block N]
//   mpcvst compat [--disable-file PATH]
#include "mpcvst/elf_inspect.h"
#include "mpcvst/offline_render.h"
#include "mpcvst/plugin_chain.h"
#include "mpcvst/plugin_registry.h"
#include "mpcvst/plugin_scanner.h"
#include "mpcvst/sampqol_compat.h"
#include "mpcvst/state_file.h"
#include "mpcvst/version.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace mpcvst;

namespace {

int usage() {
    std::fprintf(stderr,
        "mpcvst %s: standalone MPC Sample VST2 host\n"
        "  mpcvst scan  [--worker PATH] [--cache FILE] [--lib DIR]... [--rescan] DIR...\n"
        "  mpcvst info  PLUGIN.so\n"
        "  mpcvst render PLUGIN.so [--effect FX.so]... [--midi SCRIPT] [--seconds S] [--rate HZ] [--block N]\n"
        "               [--tempo BPM] [--program N] [--param I=V]... [--input IN.wav] [--state FILE]\n"
        "               [--save-state FILE] [--out OUT.wav] [--float] [--quiet]\n"
        "  mpcvst bench PLUGIN.so [--instances N] [--seconds S] [--rate HZ] [--block N]\n"
        "  mpcvst compat [--disable-file PATH]\n", MPCVST_VERSION);
    return 2;
}

std::string read_file(const std::string& path, bool& ok) {
    std::ifstream in(path);
    ok = static_cast<bool>(in);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void print_info(const PluginInfo& i) {
    std::printf("path:          %s\nname:          %s\nvendor:        %s\nproduct:       %s\n", i.path.c_str(), i.name.c_str(), i.vendor.c_str(), i.product.c_str());
    std::printf("unique id:     %d (0x%08x)\nversion:       %d  (VST %d)\nkind:          %s%s\n", i.unique_id, static_cast<unsigned>(i.unique_id), i.version, i.vst_version,
                plugin_kind_name(i.kind), i.is_synth ? " (isSynth)" : "");
    std::printf("audio:         %d in, %d out, latency %d frames\nparameters:    %d\nprograms:      %d\n", i.num_inputs, i.num_outputs, i.initial_delay, i.num_params, i.num_programs);
    std::printf("state:         %s\nmidi in:       %s\nfloat process: %s\neditor:        %s (never opened by this host)\n", i.has_chunks ? "chunk" : "parameters", i.receives_midi ? "yes" : "no",
                i.can_replace ? "yes" : "NO", i.has_editor ? "yes" : "no");
}

int cmd_info(int argc, char** argv) {
    if (argc < 1) return usage();
    std::string path = argv[0];
    ElfInfo elf;
    if (!inspect_elf(path, elf)) { std::printf("cannot inspect: %s\n", elf.error.c_str()); return 1; }
    std::printf("file:          %s (%llu bytes)\n", path.c_str(), static_cast<unsigned long long>(elf.file_size));
    if (!elf.is_elf) { std::printf("not an ELF file\n"); return 1; }
    std::printf("elf:           %s %s %s, entry %s\n", elf.class64 ? "64-bit" : "32-bit", elf_machine_name(elf.machine), elf.shared_object ? "shared object" : "NOT a shared object",
                elf.exports_vst_entry ? elf.entry_symbol.c_str() : "MISSING");
    std::printf("needs:        ");
    for (const std::string& n : elf.needed) std::printf(" %s", n.c_str());
    std::printf("\n");
    std::vector<std::string> missing = missing_dependencies(elf, {});
    for (const std::string& m : missing) std::printf("MISSING:       %s\n", m.c_str());
    if (elf.machine != host_machine()) { std::printf("not loadable here: built for %s, this host is %s\n", elf_machine_name(elf.machine), elf_machine_name(host_machine())); return 1; }
    if (!missing.empty()) return 1;
    LoadLimits limits;
    std::string error;
    auto plugin = Vst2Plugin::load(path, 44100.0, 128, limits, error);
    if (!plugin) { std::printf("load failed:   %s\n", error.c_str()); return 1; }
    print_info(plugin->info());
    for (int p = 0; p < plugin->num_params(); ++p)
        std::printf("  param %3d: %-24s = %.4f  %s %s\n", p, plugin->param_name(p).c_str(), plugin->get_param(p), plugin->param_display(p).c_str(), plugin->param_label(p).c_str());
    for (int p = 0; p < plugin->num_programs() && p < 128; ++p) std::printf("  program %3d: %s\n", p, plugin->program_name(p).c_str());
    return 0;
}

int cmd_scan(int argc, char** argv) {
    ScanPolicy policy;
    std::string cache;
    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--worker" && i + 1 < argc) policy.worker_exe = argv[++i];
        else if (a == "--cache" && i + 1 < argc) cache = argv[++i];
        else if (a == "--lib" && i + 1 < argc) policy.library_dirs.push_back(argv[++i]);
        else if (a == "--rescan") policy.rescan_unchanged = true;
        else if (a == "--timeout" && i + 1 < argc) policy.worker_timeout_ms = std::atoi(argv[++i]);
        else if (!a.empty() && a[0] != '-') policy.plugin_dirs.push_back(a);
        else return usage();
    }
    if (policy.plugin_dirs.empty()) return usage();
    PluginRegistry registry;
    std::string error;
    if (!cache.empty()) registry.load(cache, error);
    PluginScanner scanner(policy);
    ScanReport report = scanner.scan(registry, [](const std::string& p) { std::fprintf(stderr, "scanning %s\n", p.c_str()); });
    for (const PluginRecord& r : registry.records()) {
        std::printf("%-12s %s", plugin_status_name(r.status), r.path.c_str());
        if (r.status == PluginStatus::Ok) std::printf("  [%s] %s by %s, %d params", plugin_kind_name(r.info.kind), r.info.name.c_str(), r.info.vendor.c_str(), r.info.num_params);
        else if (!r.reason.empty()) std::printf("  (%s)", r.reason.c_str());
        std::printf("\n");
    }
    std::printf("candidates %d, probed %d, ok %d, rejected %d, quarantined %d, cached %d\n", report.candidates, report.probed, report.ok, report.rejected, report.quarantined, report.skipped_cached);
    for (const std::string& n : report.notes) std::printf("note: %s\n", n.c_str());
    if (!cache.empty() && !registry.save(cache, error)) { std::fprintf(stderr, "cache not saved: %s\n", error.c_str()); return 1; }
    return 0;
}

int cmd_render(int argc, char** argv) {
    if (argc < 1) return usage();
    std::string path = argv[0], midi_file, out = "render.wav", input_file, state_file, save_state;
    std::vector<std::string> effects;
    std::vector<std::pair<int, float>> params;
    EngineConfig cfg;
    RenderOptions opt;
    int program = -1;
    bool float32 = false, quiet = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--effect") effects.push_back(next());
        else if (a == "--midi") midi_file = next();
        else if (a == "--seconds") opt.seconds = std::atof(next());
        else if (a == "--rate") cfg.sample_rate = std::atof(next());
        else if (a == "--block") cfg.block_size = std::atoi(next());
        else if (a == "--tempo") opt.tempo = std::atof(next());
        else if (a == "--program") program = std::atoi(next());
        else if (a == "--param") { std::string s = next(); size_t eq = s.find('='); if (eq != std::string::npos) params.emplace_back(std::atoi(s.substr(0, eq).c_str()), static_cast<float>(std::atof(s.substr(eq + 1).c_str()))); }
        else if (a == "--input") input_file = next();
        else if (a == "--state") state_file = next();
        else if (a == "--save-state") save_state = next();
        else if (a == "--out") out = next();
        else if (a == "--float") float32 = true;
        else if (a == "--quiet") quiet = true;
        else return usage();
    }
    if (cfg.block_size < 16 || cfg.block_size > 4096) { std::fprintf(stderr, "block size must be 16..4096\n"); return 2; }
    std::string error;
    if (!midi_file.empty()) {
        bool ok;
        std::string text = read_file(midi_file, ok);
        if (!ok) { std::fprintf(stderr, "cannot read %s\n", midi_file.c_str()); return 1; }
        if (!parse_midi_script(text, opt.midi, error)) { std::fprintf(stderr, "midi script: %s\n", error.c_str()); return 1; }
    }
    if (!input_file.empty()) {
        double rate;
        if (!read_wav(input_file, opt.input_left, opt.input_right, rate, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        if (rate != cfg.sample_rate) std::fprintf(stderr, "warning: input is %.0f Hz, engine runs at %.0f Hz (no conversion)\n", rate, cfg.sample_rate);
        if (opt.seconds <= 0) opt.seconds = static_cast<double>(opt.input_left.size()) / cfg.sample_rate;
    }
    PluginChain chain(cfg);
    LoadLimits limits;
    auto plugin = Vst2Plugin::load(path, cfg.sample_rate, cfg.block_size, limits, error);
    if (!plugin) { std::fprintf(stderr, "load failed: %s\n", error.c_str()); return 1; }
    if (program >= 0) plugin->set_program(program);
    for (const auto& p : params) plugin->set_param(p.first, p.second);
    if (!state_file.empty()) {
        PluginState st;
        if (!load_state_file(state_file, st, error)) { std::fprintf(stderr, "state: %s\n", error.c_str()); return 1; }
        if (!apply_plugin_state(*plugin, st, error)) { std::fprintf(stderr, "state: %s\n", error.c_str()); return 1; }
        if (!error.empty()) std::fprintf(stderr, "state: %s\n", error.c_str());
    }
    Vst2Plugin* raw = plugin.get();
    bool ok = plugin->info().kind == PluginKind::Effect ? chain.add_effect(std::move(plugin), error) : chain.set_instrument(std::move(plugin), error);
    if (!ok) { std::fprintf(stderr, "cannot use plug-in: %s\n", error.c_str()); return 1; }
    for (const std::string& fx : effects) {
        auto e = Vst2Plugin::load(fx, cfg.sample_rate, cfg.block_size, limits, error);
        if (!e) { std::fprintf(stderr, "effect %s: %s\n", fx.c_str(), error.c_str()); return 1; }
        if (!chain.add_effect(std::move(e), error)) { std::fprintf(stderr, "effect %s: %s\n", fx.c_str(), error.c_str()); return 1; }
    }
    RenderResult r = render_offline(chain, opt);
    if (!save_state.empty()) {
        PluginState st;
        capture_plugin_state(*raw, st);
        if (!save_state_file(save_state, st, error)) { std::fprintf(stderr, "save state: %s\n", error.c_str()); return 1; }
    }
    if (!write_wav(out, r.left, r.right, cfg.sample_rate, float32, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    if (!quiet) {
        std::printf("rendered %.2f s to %s: peak %.3f, rms %.4f, %llu blocks, avg %.1f us/block (%.1f%% of %.1f us), max %.1f us (%.1f%%), %llu over budget, %llu bad samples zeroed, %llu MIDI dropped\n",
                    opt.seconds, out.c_str(), r.peak, r.rms, static_cast<unsigned long long>(r.perf.blocks), r.perf.average_ns() / 1000.0, r.perf.load_percent(),
                    r.perf.budget_ns / 1000.0, r.perf.max_ns / 1000.0, r.perf.peak_percent(), static_cast<unsigned long long>(r.perf.overloads),
                    static_cast<unsigned long long>(r.bad_samples), static_cast<unsigned long long>(r.midi_dropped));
    }
    chain.clear();
    return 0;
}

int cmd_bench(int argc, char** argv) {
    if (argc < 1) return usage();
    std::string path = argv[0];
    int instances = 1;
    double seconds = 5.0;
    EngineConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--instances") instances = std::atoi(next());
        else if (a == "--seconds") seconds = std::atof(next());
        else if (a == "--rate") cfg.sample_rate = std::atof(next());
        else if (a == "--block") cfg.block_size = std::atoi(next());
        else return usage();
    }
    std::printf("machine: %s, rate %.0f Hz, block %d frames (budget %.1f us)\n", elf_machine_name(host_machine()), cfg.sample_rate, cfg.block_size, 1.0e6 * cfg.block_size / cfg.sample_rate);
    std::string error;
    LoadLimits limits;
    auto probe = Vst2Plugin::load(path, cfg.sample_rate, cfg.block_size, limits, error);
    if (!probe) { std::fprintf(stderr, "load failed: %s\n", error.c_str()); return 1; }
    const bool is_effect = probe->info().kind == PluginKind::Effect;
    std::printf("plug-in: %s (%s)\n", probe->info().name.c_str(), plugin_kind_name(probe->info().kind));
    probe.reset();

    // A chord every half second so instruments have voices to render; one
    // chord's six notes on each of the 16 channels so every instance hears it.
    std::vector<ScriptEvent> script;
    for (double t = 0.0; t < seconds; t += 0.5)
        for (int note : {48, 55, 60, 64, 67, 72}) {
            ScriptEvent on; on.seconds = t; on.data[0] = 0x90; on.data[1] = static_cast<uint8_t>(note); on.data[2] = 100; on.length = 3;
            ScriptEvent off = on; off.seconds = t + 0.4; off.data[0] = 0x80;
            script.push_back(on); script.push_back(off);
        }
    const uint64_t total = static_cast<uint64_t>(seconds * cfg.sample_rate);
    std::vector<float> l(static_cast<size_t>(cfg.block_size)), r(static_cast<size_t>(cfg.block_size)), mix_l(l), mix_r(r);

    // 0 instances: the empty host. Then 1, 2, 4, ... up to `instances`.
    // Instruments: one chain per instance, all rendered in the same block
    // loop and summed (as N pads with N plug-ins would be). Effects: one
    // chain with N effects in series.
    for (int n = 0; n <= instances; n = n == 0 ? 1 : n * 2) {
        std::vector<std::unique_ptr<PluginChain>> chains;
        cfg.max_plugins = std::max(1, n);
        const int chain_count = is_effect ? 1 : std::max(1, n);
        for (int c = 0; c < chain_count; ++c) chains.push_back(std::make_unique<PluginChain>(cfg));
        for (int k = 0; k < n; ++k) {
            auto p = Vst2Plugin::load(path, cfg.sample_rate, cfg.block_size, limits, error);
            if (!p) { std::fprintf(stderr, "load failed: %s\n", error.c_str()); return 1; }
            PluginChain& chain = *chains[is_effect ? 0 : static_cast<size_t>(k)];
            bool ok = is_effect ? chain.add_effect(std::move(p), error) : chain.set_instrument(std::move(p), error);
            if (!ok) { std::fprintf(stderr, "instance %d: %s\n", k, error.c_str()); return 1; }
        }
        for (auto& c : chains) { c->transport().set_tempo(120.0); c->transport().set_playing(true); c->transport().rewind(); }
        PerfMonitor perf;
        perf.configure(cfg.sample_rate, cfg.block_size);
        size_t next_event = 0;
        uint64_t pos = 0;
        float peak = 0.0f;
        std::vector<float> ol(static_cast<size_t>(cfg.block_size)), orr(static_cast<size_t>(cfg.block_size));
        while (pos < total) {
            const int frames = static_cast<int>(std::min<uint64_t>(static_cast<uint64_t>(cfg.block_size), total - pos));
            const uint64_t block_end = pos + static_cast<uint64_t>(frames);
            while (next_event < script.size()) {
                const ScriptEvent& e = script[next_event];
                uint64_t frame = static_cast<uint64_t>(e.seconds * cfg.sample_rate);
                if (frame >= block_end) break;
                for (auto& c : chains) c->midi().push(frame, e.data, e.length);
                ++next_event;
            }
            // For effects, feed noise-free input: a sine so the filter has work.
            if (is_effect) for (int i = 0; i < frames; ++i) l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = 0.5f * std::sin(static_cast<float>(pos + static_cast<uint64_t>(i)) * 0.05f);
            const uint64_t t0 = perf.begin();
            std::fill(mix_l.begin(), mix_l.end(), 0.0f);
            std::fill(mix_r.begin(), mix_r.end(), 0.0f);
            for (auto& c : chains) {
                c->process(is_effect ? l.data() : nullptr, is_effect ? r.data() : nullptr, ol.data(), orr.data(), frames);
                for (int i = 0; i < frames; ++i) { mix_l[static_cast<size_t>(i)] += ol[static_cast<size_t>(i)]; mix_r[static_cast<size_t>(i)] += orr[static_cast<size_t>(i)]; }
            }
            perf.end(t0);
            for (int i = 0; i < frames; ++i) peak = std::max(peak, std::fabs(mix_l[static_cast<size_t>(i)]));
            pos = block_end;
        }
        PerfSnapshot s = perf.snapshot();
        std::printf("%2d instance(s): avg %8.1f us/block (%6.2f%%), max %8.1f us (%6.2f%%), %llu/%llu blocks over budget, peak %.3f\n", n, s.average_ns() / 1000.0, s.load_percent(),
                    s.max_ns / 1000.0, s.peak_percent(), static_cast<unsigned long long>(s.overloads), static_cast<unsigned long long>(s.blocks), peak);
        for (auto& c : chains) c->clear();
        if (n >= instances) break;
    }
    std::printf("note: offline timing on this machine; only a run on the MPC Sample itself measures the device.\n");
    return 0;
}

int cmd_compat(int argc, char** argv) {
    std::string disable;
    for (int i = 0; i < argc; ++i) if (!std::strcmp(argv[i], "--disable-file") && i + 1 < argc) disable = argv[++i];
    HostEnvironment env = detect_environment(disable);
    CompatDecision d = decide(env, true);
    std::printf("process:        %s\napp build id:   %s\nSampQOL loaded: %s\nLD_PRELOAD:     %s\ndisable file:   %s\n", env.exe_path.c_str(),
                env.app_build_id.empty() ? "(none)" : env.app_build_id.c_str(), env.sampqol_preloaded ? "yes" : "no", env.ld_preload.c_str(), env.disable_file_present ? "present" : "absent");
    std::printf("identity known: %s\nintegration:    %s\nreason:         %s\n", d.identity_known ? "yes" : "no", d.enabled ? "ENABLED" : "disabled", d.reason.c_str());
    std::printf("known builds:\n");
    for (const CompatEntry& e : compat_table())
        std::printf("  %s fw %s audio %s midi %s: %s\n", e.build_id, e.firmware, e.audio_hook_verified ? "verified" : "unverified", e.midi_hook_verified ? "verified" : "unverified", e.note);
    return d.enabled ? 0 : 3;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::string cmd = argv[1];
    if (cmd == "info") return cmd_info(argc - 2, argv + 2);
    if (cmd == "scan") return cmd_scan(argc - 2, argv + 2);
    if (cmd == "render") return cmd_render(argc - 2, argv + 2);
    if (cmd == "bench") return cmd_bench(argc - 2, argv + 2);
    if (cmd == "compat") return cmd_compat(argc - 2, argv + 2);
    if (cmd == "--version" || cmd == "version") { std::printf("mpcvst %s\n", MPCVST_VERSION); return 0; }
    return usage();
}
