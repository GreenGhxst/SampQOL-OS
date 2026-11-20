// SPDX-License-Identifier: MIT
// Timestamped MIDI from one control thread to the audio thread: a single-
// producer single-consumer ring. Events carry an absolute frame time; the
// audio thread takes those that fall in its block, in deterministic order
// (frame, then arrival).
#pragma once

#include "mpcvst/types.h"

#include <atomic>
#include <cstdint>

namespace mpcvst {

struct TimedMidiEvent {
    uint64_t frame = 0;     // absolute engine frame; 0 means "as soon as possible"
    uint32_t seq = 0;       // arrival order, set by the queue
    uint8_t data[3] = {0, 0, 0};
    uint8_t length = 0;
};

template <unsigned CapacityPow2>
class MidiQueue {
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0, "capacity must be a power of two");

public:
    static constexpr unsigned capacity = CapacityPow2;

    // Producer. False when full (the event is dropped and counted).
    bool push(uint64_t frame, const uint8_t* bytes, uint8_t length) {
        if (length == 0 || length > 3) return false;
        unsigned head = head_.load(std::memory_order_relaxed);
        unsigned tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= capacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        TimedMidiEvent& e = ring_[head & (capacity - 1)];
        e.frame = frame;
        e.seq = seq_++;
        e.length = length;
        for (int i = 0; i < 3; ++i) e.data[i] = i < length ? bytes[i] : 0;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    bool push(uint64_t frame, uint8_t status, uint8_t d1, uint8_t d2) {
        uint8_t b[3] = {status, d1, d2};
        uint8_t len = (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0 ? 2 : 3;
        return push(frame, b, len);
    }

    // Consumer (audio thread). Moves every event due before block_start +
    // frames into `out` (at most max_out), with delta_frames inside the
    // block; events already late get delta 0. Events not yet due stay in
    // the ring, in their arrival order, whatever order they were pushed in:
    // the consumer owns [tail, head) and compacts it in place. Returns the
    // count, sorted by (delta, arrival) so the plug-in sees a deterministic
    // order. No allocation; at most `capacity` steps.
    int drain(uint64_t block_start, int frames, MidiEvent* out, int max_out) {
        const unsigned tail = tail_.load(std::memory_order_relaxed);
        const unsigned head = head_.load(std::memory_order_acquire);
        const uint64_t block_end = block_start + static_cast<uint64_t>(frames);
        int n = 0;
        // Pass 1: take what is due, keep the rest (compacted towards head so
        // that tail can move forward past the taken ones).
        unsigned keep = head; // next slot (exclusive) for kept events, walking down
        for (unsigned i = head; i != tail; --i) {
            const TimedMidiEvent& e = ring_[(i - 1) & (capacity - 1)];
            if (e.frame < block_end && n < max_out) {
                MidiEvent& m = out[n++];
                m.delta_frames = e.frame <= block_start ? 0 : static_cast<int32_t>(e.frame - block_start);
                m.length = e.length;
                m.data[0] = e.data[0];
                m.data[1] = e.data[1];
                m.data[2] = e.data[2];
                taken_seq_[n - 1] = e.seq;
            } else {
                --keep;
                if (keep != i - 1) ring_[keep & (capacity - 1)] = e;
            }
        }
        tail_.store(keep, std::memory_order_release);
        // Pass 2: the taken events were collected newest first; order them by
        // (delta, arrival) with an insertion sort (small n, no allocation).
        for (int i = 1; i < n; ++i) {
            MidiEvent key = out[i];
            uint32_t key_seq = taken_seq_[i];
            int j = i - 1;
            while (j >= 0 && (out[j].delta_frames > key.delta_frames || (out[j].delta_frames == key.delta_frames && taken_seq_[j] > key_seq))) {
                out[j + 1] = out[j];
                taken_seq_[j + 1] = taken_seq_[j];
                --j;
            }
            out[j + 1] = key;
            taken_seq_[j + 1] = key_seq;
        }
        return n;
    }

    unsigned pending() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }
    uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    void clear() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

private:
    TimedMidiEvent ring_[capacity];
    std::atomic<unsigned> head_{0}, tail_{0};
    std::atomic<uint64_t> dropped_{0};
    uint32_t seq_ = 0;
    uint32_t taken_seq_[capacity]; // consumer scratch: arrival order of the events taken
};

} // namespace mpcvst
