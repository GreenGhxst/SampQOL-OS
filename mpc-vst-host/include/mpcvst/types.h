// SPDX-License-Identifier: MIT
// Shared value types of the MPC Sample VST host: plug-in metadata, MIDI
// events as the engine carries them, and the transport state a plug-in reads.
#pragma once

#include <cstdint>
#include <string>

namespace mpcvst {

enum class PluginKind : uint8_t { Unknown, Instrument, Effect };

const char* plugin_kind_name(PluginKind kind);

// What the host learns about a plug-in once its library is open.
struct PluginInfo {
    std::string path;
    std::string name;
    std::string vendor;
    std::string product;
    int32_t unique_id = 0;
    int32_t version = 0;
    int32_t vst_version = 0;
    int32_t vendor_version = 0;
    int32_t num_params = 0;
    int32_t num_programs = 0;
    int32_t num_inputs = 0;
    int32_t num_outputs = 0;
    int32_t initial_delay = 0;
    int32_t flags = 0;
    int32_t category = 0;
    PluginKind kind = PluginKind::Unknown;
    bool is_synth = false;
    bool has_chunks = false;
    bool can_replace = false;
    bool can_double_replace = false;
    bool receives_midi = false;
    bool has_editor = false;
};

// A MIDI channel message with its position inside the audio block.
struct MidiEvent {
    int32_t delta_frames = 0;   // frames from the start of the block
    uint8_t data[3] = {0, 0, 0};
    uint8_t length = 0;         // 1..3 bytes
};

// The host transport as a plug-in reads it through MVST_HOST_GET_TIME.
struct TransportState {
    bool playing = false;
    double sample_pos = 0.0;    // frames from the start of the song
    double tempo = 120.0;       // BPM
    double ppq_pos = 0.0;       // quarter notes from the start of the song
    double bar_start_pos = 0.0; // ppq of the current bar's first beat
    int32_t time_sig_numerator = 4;
    int32_t time_sig_denominator = 4;
};

// Limits a host applies before it trusts a plug-in's self-description.
struct LoadLimits {
    int32_t max_inputs = 16;
    int32_t max_outputs = 16;
    int32_t max_params = 4096;
    int32_t max_programs = 4096;
    int32_t max_initial_delay = 48000 * 10;
    size_t max_chunk_bytes = 16u << 20;
    bool require_replacing = true;
};

} // namespace mpcvst
