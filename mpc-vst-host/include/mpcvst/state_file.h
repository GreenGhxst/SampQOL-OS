// SPDX-License-Identifier: MIT
// Sidecar plug-in state: what a pad or track needs to bring a plug-in back
// the way it was. Saved outside the stock project files, in the expansion's
// own folder, as one small file per slot. A missing plug-in keeps its state.
//
// Layout (all little-endian):
//   "MVXS" u32 version(1)
//   u32 header_len, header: UTF-8 "key=value\n" lines
//   u32 body_kind (0 none, 1 plug-in chunk, 2 parameter floats)
//   u32 body_len, body
//   u32 crc32 of everything before it
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mpcvst {

enum class StateBodyKind : uint32_t { None = 0, Chunk = 1, Parameters = 2 };

struct PluginState {
    // identity
    std::string plugin_path;      // where it was loaded from (relative to the card when possible)
    std::string plugin_name;
    int32_t unique_id = 0;
    int32_t plugin_version = 0;
    // placement
    int pad = -1;                 // 0..127, -1 none
    int track = -1;
    int midi_channel = 0;         // 0 omni, 1..16
    int transpose = 0;
    std::vector<std::string> resource_files; // files the plug-in needs beside itself
    // sound
    int program = 0;
    StateBodyKind body_kind = StateBodyKind::None;
    std::vector<uint8_t> chunk;   // body_kind Chunk
    std::vector<float> params;    // body_kind Parameters
    // free-form extras kept for forward compatibility
    std::vector<std::pair<std::string, std::string>> extra;
};

uint32_t crc32_of(const uint8_t* data, size_t size, uint32_t seed = 0);

std::vector<uint8_t> encode_state(const PluginState& state);
// False on a damaged file (bad magic, truncated, CRC mismatch); the state is untouched then.
bool decode_state(const uint8_t* data, size_t size, PluginState& state, std::string& error);

bool save_state_file(const std::string& path, const PluginState& state, std::string& error);
bool load_state_file(const std::string& path, PluginState& state, std::string& error);

class Vst2Plugin;
// Captures the plug-in's sound: its chunk when it has one, else every parameter.
void capture_plugin_state(const Vst2Plugin& plugin, PluginState& state);
// Restores program then chunk or parameters. Returns false when the identity
// does not match (different unique id) and nothing was applied.
bool apply_plugin_state(Vst2Plugin& plugin, const PluginState& state, std::string& error);

} // namespace mpcvst
