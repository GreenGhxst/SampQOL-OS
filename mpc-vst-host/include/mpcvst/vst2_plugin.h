// SPDX-License-Identifier: MIT
// One loaded VST2 plug-in: its library, its effect, its lifecycle, and the
// real-time entry point the audio engine calls.
//
// Threading: load(), activate(), deactivate(), the parameter, program and
// state calls and the destructor belong to one control thread. process()
// belongs to the audio thread; it allocates nothing, locks nothing and
// makes no system call. The audio thread must have stopped calling
// process() before deactivate() or the destructor run (the engine's job).
//
// The plug-in runs in this process. Nothing here can contain a crash in
// plug-in code: that is what the scanner's worker process is for. Treat an
// in-process plug-in as experimental until it has run clean on the device.
#pragma once

#include "mpcvst/types.h"
#include "mpcvst/vst2_abi.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mpcvst {

struct HostCallbackStats {
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> unanswered{0};  // opcodes this host does not implement
    std::atomic<uint64_t> automate{0};    // the plug-in changed a parameter itself
    std::atomic<uint64_t> io_changed{0};
    std::atomic<int32_t> last_unanswered{-1};
};

class Vst2Plugin {
public:
    // Opens the library, calls its entry point, validates the effect and
    // runs MVST_OP_OPEN. nullptr and `error` on any failure, with the
    // library closed again. The sample rate and block size are what the
    // plug-in is told at open; activate() may change them.
    static std::unique_ptr<Vst2Plugin> load(const std::string& path, double sample_rate, int block_size,
                                            const LoadLimits& limits, std::string& error);
    ~Vst2Plugin();

    Vst2Plugin(const Vst2Plugin&) = delete;
    Vst2Plugin& operator=(const Vst2Plugin&) = delete;

    const PluginInfo& info() const { return info_; }
    double sample_rate() const { return sample_rate_; }
    int block_size() const { return block_size_; }
    int max_events() const { return max_events_; }

    // Tells the plug-in the rate and block, switches it on and allocates
    // the audio and event buffers for `block_size` frames. Safe to call
    // again with other values once the audio thread has stopped.
    bool activate(double sample_rate, int block_size, int max_events, std::string& error);
    void deactivate();
    bool active() const { return active_; }

    // ---- real-time ----
    // Runs one block of at most block_size() frames. `inputs` holds
    // num_inputs channel pointers (unused channels may be nullptr: silence),
    // `outputs` num_outputs channel pointers (nullptr: discarded). Events
    // are delivered before the audio; those past `frames` are dropped.
    // Returns false and writes silence when the plug-in is bypassed or not
    // active. Never allocates.
    bool process(const float* const* inputs, float* const* outputs, int frames,
                 const MidiEvent* events, int num_events, const TransportState& transport);

    // Bypass: process() writes silence (an effect: copies input to output).
    void set_bypass(bool on) { bypass_.store(on, std::memory_order_relaxed); }
    bool bypassed() const { return bypass_.load(std::memory_order_relaxed); }

    // How many output samples were not finite and were replaced by zero.
    uint64_t bad_samples() const { return bad_samples_.load(std::memory_order_relaxed); }

    // ---- parameters (control thread) ----
    int num_params() const { return info_.num_params; }
    float get_param(int index) const;
    void set_param(int index, float value);
    std::string param_name(int index) const;
    std::string param_label(int index) const;
    std::string param_display(int index) const;
    bool param_automatable(int index) const;

    // ---- programs ----
    int num_programs() const { return info_.num_programs; }
    int current_program() const;
    void set_program(int index);
    std::string program_name(int index) const;

    // ---- state ----
    // The plug-in's chunk when it has one (MVST_FLAG_PROGRAM_CHUNKS), else
    // nothing. The data is copied out of the plug-in's own buffer.
    bool get_chunk(std::vector<uint8_t>& out) const;
    bool set_chunk(const uint8_t* data, size_t size);

    int tail_size() const;
    bool can_do(const char* what) const;

    const HostCallbackStats& callback_stats() const { return callbacks_; }

private:
    Vst2Plugin(void* library, mvst_effect* effect, std::string path);
    static intptr_t host_callback(mvst_effect* e, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
    intptr_t answer(int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
    intptr_t dispatch(int32_t opcode, int32_t index = 0, intptr_t value = 0, void* ptr = nullptr, float opt = 0.0f) const;
    std::string dispatch_string(int32_t opcode, int32_t index = 0) const;
    void read_info();
    void free_buffers();
    void update_time(const TransportState& transport, int frames);

    void* library_;
    mvst_effect* effect_;
    PluginInfo info_;
    double sample_rate_ = 44100.0;
    int block_size_ = 0;
    int max_events_ = 0;
    bool active_ = false;
    bool opened_ = false;
    std::atomic<bool> bypass_{false};
    std::atomic<uint64_t> bad_samples_{0};
    mvst_time_info time_{};
    bool was_playing_ = false;

    // Preallocated at activate(): planar audio, the event list and the
    // MIDI events it points to.
    std::vector<float> audio_in_, audio_out_;
    std::vector<float*> in_ptrs_, out_ptrs_;
    std::vector<mvst_midi_event> midi_;
    std::vector<uint8_t> events_blob_; // mvst_events with max_events_ pointers
    mvst_events* events_ = nullptr;

    HostCallbackStats callbacks_;
};

} // namespace mpcvst
