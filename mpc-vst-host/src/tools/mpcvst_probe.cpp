// SPDX-License-Identifier: MIT
// mpcvst-probe: the scanner's worker. Loads one plug-in in this process,
// opens it, activates it, processes a few blocks of silence (and a note for
// an instrument), and prints what it learned as key=value lines. If the
// plug-in crashes or hangs, this process dies and the scanner notices; the
// host that started it is untouched.
#include "mpcvst/vst2_plugin.h"
#include "mpcvst/perf_monitor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace mpcvst;

static void kv(const char* k, const std::string& v) {
    std::string clean;
    for (char c : v) clean.push_back(c == '\n' || c == '\r' ? ' ' : c);
    std::printf("%s=%s\n", k, clean.c_str());
}
static void kv(const char* k, long long v) { std::printf("%s=%lld\n", k, v); }

int main(int argc, char** argv) {
    double rate = 44100.0;
    int block = 128;
    const char* path = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--rate") && i + 1 < argc) rate = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--block") && i + 1 < argc) block = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--")) { if (i + 1 < argc) path = argv[++i]; }
        else if (argv[i][0] != '-') path = argv[i];
    }
    if (!path) {
        std::fprintf(stderr, "usage: mpcvst-probe [--rate HZ] [--block FRAMES] -- plugin.so\n");
        kv("result", "failed");
        kv("message", "no plug-in path");
        return 2;
    }
    if (block < 16 || block > 4096) block = 128;
    setvbuf(stdout, nullptr, _IOLBF, 0); // every line reaches the parent even if we die next

    LoadLimits limits;
    std::string error;
    std::unique_ptr<Vst2Plugin> plugin = Vst2Plugin::load(path, rate, block, limits, error);
    if (!plugin) {
        kv("result", "rejected");
        kv("message", error);
        return 0;
    }
    const PluginInfo& info = plugin->info();
    kv("name", info.name);
    kv("vendor", info.vendor);
    kv("product", info.product);
    kv("unique_id", info.unique_id);
    kv("version", info.version);
    kv("vst_version", info.vst_version);
    kv("num_params", info.num_params);
    kv("num_programs", info.num_programs);
    kv("num_inputs", info.num_inputs);
    kv("num_outputs", info.num_outputs);
    kv("initial_delay", info.initial_delay);
    kv("flags", info.flags);
    kv("category", info.category);
    kv("kind", plugin_kind_name(info.kind));
    kv("is_synth", info.is_synth ? 1 : 0);
    kv("has_chunks", info.has_chunks ? 1 : 0);
    kv("can_replace", info.can_replace ? 1 : 0);
    kv("receives_midi", info.receives_midi ? 1 : 0);
    kv("has_editor", info.has_editor ? 1 : 0);

    if (!plugin->activate(rate, block, 64, error)) {
        kv("result", "rejected");
        kv("message", "activate failed: " + error);
        return 0;
    }
    std::vector<float> in(static_cast<size_t>(block) * 16, 0.0f), out(static_cast<size_t>(block) * 16, 0.0f);
    std::vector<const float*> ins(16);
    std::vector<float*> outs(16);
    for (int c = 0; c < 16; ++c) { ins[static_cast<size_t>(c)] = in.data() + c * block; outs[static_cast<size_t>(c)] = out.data() + c * block; }
    MidiEvent note[2];
    note[0].data[0] = 0x90; note[0].data[1] = 60; note[0].data[2] = 100; note[0].length = 3; note[0].delta_frames = 0;
    note[1].data[0] = 0x80; note[1].data[1] = 60; note[1].data[2] = 0; note[1].length = 3; note[1].delta_frames = 0;
    TransportState t;
    t.playing = true;
    const uint64_t t0 = monotonic_ns();
    const int blocks = 8;
    for (int b = 0; b < blocks; ++b) {
        plugin->process(ins.data(), outs.data(), block, b == 0 ? &note[0] : b == blocks - 1 ? &note[1] : nullptr, b == 0 || b == blocks - 1 ? 1 : 0, t);
        t.sample_pos += block;
    }
    const double per_block_ms = static_cast<double>(monotonic_ns() - t0) / 1.0e6 / blocks;
    std::printf("process_block_ms=%.4f\n", per_block_ms);
    kv("bad_samples", static_cast<long long>(plugin->bad_samples()));
    kv("unanswered_host_opcodes", static_cast<long long>(plugin->callback_stats().unanswered.load()));

    // A chunk round trip, when it has one: a plug-in that cannot save
    // itself is still usable, so this is a note, not a rejection.
    if (info.has_chunks) {
        std::vector<uint8_t> chunk;
        kv("chunk_bytes", plugin->get_chunk(chunk) ? static_cast<long long>(chunk.size()) : -1LL);
    }
    plugin->deactivate();
    kv("result", "ok");
    kv("message", "loaded, opened, activated and processed " + std::to_string(blocks) + " blocks");
    plugin.reset();
    return 0;
}
