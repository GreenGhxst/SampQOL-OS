// SPDX-License-Identifier: MIT
// The audio engine adapter: one instrument followed by effects, fed from the
// MIDI queue and the transport, rendered into stereo buffers with the
// performance monitor around it. This is what both the standalone test
// adapter and a later SampQOL adapter drive; neither touches a plug-in
// directly.
#pragma once

#include "mpcvst/midi_queue.h"
#include "mpcvst/perf_monitor.h"
#include "mpcvst/transport.h"
#include "mpcvst/vst2_plugin.h"

#include <memory>
#include <string>
#include <vector>

namespace mpcvst {

struct EngineConfig {
    double sample_rate = 44100.0;
    int block_size = 128;        // the MPC Sample's stock audio block (sample-base: 128 frames)
    int max_events_per_block = 256;
    int max_plugins = 8;
    int max_channels = 2;        // stereo through the chain
};

class PluginChain {
public:
    explicit PluginChain(EngineConfig config);
    ~PluginChain();

    const EngineConfig& config() const { return config_; }

    // Control thread. The audio thread must not be inside process() while
    // the chain changes: the caller stops it (or, offline, there is none).
    bool set_instrument(std::unique_ptr<Vst2Plugin> plugin, std::string& error);
    bool add_effect(std::unique_ptr<Vst2Plugin> plugin, std::string& error);
    void clear();
    Vst2Plugin* instrument() { return instrument_.get(); }
    std::vector<std::unique_ptr<Vst2Plugin>>& effects() { return effects_; }
    size_t plugin_count() const { return (instrument_ ? 1 : 0) + effects_.size(); }

    MidiQueue<1024>& midi() { return midi_; }
    Transport& transport() { return transport_; }
    const PerfMonitor& perf() const { return perf_; }
    PerfMonitor& perf() { return perf_; }

    // Emergency bypass of the whole chain: silence out, plug-ins untouched.
    void set_bypass_all(bool on) { bypass_all_.store(on, std::memory_order_relaxed); }
    bool bypassed_all() const { return bypass_all_.load(std::memory_order_relaxed); }

    // Audio thread. `in` may be nullptr (no input). Writes `frames` stereo
    // frames to out_left/out_right. frames <= block_size. No allocation.
    void process(const float* in_left, const float* in_right, float* out_left, float* out_right, int frames);

    // All notes off and sustain off on every channel, delivered at the next
    // block (control thread).
    void all_notes_off();

private:
    EngineConfig config_;
    std::unique_ptr<Vst2Plugin> instrument_;
    std::vector<std::unique_ptr<Vst2Plugin>> effects_;
    MidiQueue<1024> midi_;
    Transport transport_;
    PerfMonitor perf_;
    std::atomic<bool> bypass_all_{false};
    std::vector<MidiEvent> events_;
    std::vector<float> bus_a_, bus_b_;       // interleaved planar: [ch][frame]
    std::vector<float*> ptr_a_, ptr_b_;
    std::vector<const float*> const_ptrs_;
    const size_t channels_;
};

} // namespace mpcvst
