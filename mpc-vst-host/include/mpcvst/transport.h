// SPDX-License-Identifier: MIT
// The host transport: tempo, play state and the song position in frames
// and quarter notes, advanced by the audio thread once per block.
#pragma once

#include "mpcvst/types.h"

#include <atomic>
#include <cmath>
#include <cstdint>

namespace mpcvst {

class Transport {
public:
    void configure(double sample_rate) { sample_rate_ = sample_rate; }

    // Control thread.
    void set_tempo(double bpm) { tempo_.store(bpm > 20.0 && bpm < 999.0 ? bpm : 120.0, std::memory_order_relaxed); }
    void set_playing(bool on) { playing_.store(on, std::memory_order_relaxed); }
    void set_time_signature(int num, int den) { sig_num_.store(num > 0 ? num : 4); sig_den_.store(den > 0 ? den : 4); }
    void rewind() { rewind_.store(true, std::memory_order_relaxed); }

    // Audio thread: the state for the block that starts now.
    TransportState state() const {
        TransportState t;
        t.playing = playing_.load(std::memory_order_relaxed);
        t.tempo = tempo_.load(std::memory_order_relaxed);
        t.sample_pos = static_cast<double>(frame_);
        t.ppq_pos = ppq_;
        t.time_sig_numerator = sig_num_.load(std::memory_order_relaxed);
        t.time_sig_denominator = sig_den_.load(std::memory_order_relaxed);
        const double beats_per_bar = 4.0 * t.time_sig_numerator / t.time_sig_denominator;
        t.bar_start_pos = beats_per_bar > 0 ? ppq_ - std::fmod(ppq_, beats_per_bar) : 0.0;
        return t;
    }

    // Audio thread, after the block.
    void advance(int frames) {
        if (rewind_.exchange(false, std::memory_order_relaxed)) { frame_ = 0; ppq_ = 0.0; }
        if (!playing_.load(std::memory_order_relaxed)) return;
        frame_ += static_cast<uint64_t>(frames);
        ppq_ += static_cast<double>(frames) * tempo_.load(std::memory_order_relaxed) / (60.0 * sample_rate_);
    }

    uint64_t frame() const { return frame_; }

private:
    double sample_rate_ = 44100.0;
    std::atomic<double> tempo_{120.0};
    std::atomic<bool> playing_{false};
    std::atomic<bool> rewind_{false};
    std::atomic<int> sig_num_{4}, sig_den_{4};
    uint64_t frame_ = 0;
    double ppq_ = 0.0;
};

} // namespace mpcvst
