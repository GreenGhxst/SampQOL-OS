// SPDX-License-Identifier: MIT
// The standalone test adapter: drives a PluginChain with a scripted MIDI
// sequence and an optional input WAV, writes the output WAV, reports timing.
// Used by the tests, the CLI and the benchmark; it never touches hardware.
#pragma once

#include "mpcvst/plugin_chain.h"

#include <string>
#include <vector>

namespace mpcvst {

// A MIDI script line: "<seconds> <event> ..." with events
//   on <ch 1-16> <note> <vel>, off <ch> <note> [vel], cc <ch> <cc> <val>,
//   bend <ch> <value -8192..8191>, pc <ch> <program>, sustain <ch> on|off, allnotesoff
// '#' starts a comment. Times may be in any order; the engine orders them.
struct ScriptEvent {
    double seconds = 0.0;
    uint8_t data[3] = {0, 0, 0};
    uint8_t length = 0;
};

bool parse_midi_script(const std::string& text, std::vector<ScriptEvent>& out, std::string& error);

struct RenderOptions {
    double seconds = 2.0;
    double tempo = 120.0;
    bool playing = true;
    std::vector<ScriptEvent> midi;
    std::vector<float> input_left, input_right; // optional effect input, at the engine's rate
};

struct RenderResult {
    std::vector<float> left, right;
    PerfSnapshot perf;
    uint64_t bad_samples = 0;     // non-finite outputs the host zeroed
    float peak = 0.0f;
    double rms = 0.0;
    uint64_t midi_dropped = 0;
};

// Renders block by block. The chain must already hold its plug-ins.
RenderResult render_offline(PluginChain& chain, const RenderOptions& options);

// WAV, 16-bit or 32-bit float PCM. Reads mono or stereo; writes stereo.
bool write_wav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right, double sample_rate,
               bool float32, std::string& error);
bool read_wav(const std::string& path, std::vector<float>& left, std::vector<float>& right, double& sample_rate, std::string& error);

} // namespace mpcvst
