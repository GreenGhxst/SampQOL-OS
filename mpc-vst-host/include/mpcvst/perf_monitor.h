// SPDX-License-Identifier: MIT
// Per-block timing of the audio callback: how long processing took against
// the block's budget. The audio thread writes with relaxed atomics (one
// clock_gettime per block, a vDSO call, no system call); any thread reads.
#pragma once

#include <atomic>
#include <cstdint>
#include <time.h>

namespace mpcvst {

inline uint64_t monotonic_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000000000ull + static_cast<uint64_t>(t.tv_nsec);
}

struct PerfSnapshot {
    uint64_t blocks = 0;
    uint64_t overloads = 0;      // blocks over budget
    uint64_t last_ns = 0;
    uint64_t max_ns = 0;
    uint64_t total_ns = 0;
    uint64_t budget_ns = 0;
    double average_ns() const { return blocks ? static_cast<double>(total_ns) / static_cast<double>(blocks) : 0.0; }
    double load_percent() const { return budget_ns ? 100.0 * average_ns() / static_cast<double>(budget_ns) : 0.0; }
    double peak_percent() const { return budget_ns ? 100.0 * static_cast<double>(max_ns) / static_cast<double>(budget_ns) : 0.0; }
};

class PerfMonitor {
public:
    void configure(double sample_rate, int block_size) {
        budget_ns_.store(sample_rate > 0 ? static_cast<uint64_t>(1.0e9 * block_size / sample_rate) : 0, std::memory_order_relaxed);
    }
    void reset() {
        blocks_.store(0); overloads_.store(0); last_.store(0); max_.store(0); total_.store(0);
    }

    // Audio thread.
    uint64_t begin() const { return monotonic_ns(); }
    void end(uint64_t t0) {
        const uint64_t dt = monotonic_ns() - t0;
        blocks_.fetch_add(1, std::memory_order_relaxed);
        total_.fetch_add(dt, std::memory_order_relaxed);
        last_.store(dt, std::memory_order_relaxed);
        uint64_t m = max_.load(std::memory_order_relaxed);
        while (dt > m && !max_.compare_exchange_weak(m, dt, std::memory_order_relaxed)) {}
        const uint64_t budget = budget_ns_.load(std::memory_order_relaxed);
        if (budget && dt > budget) overloads_.fetch_add(1, std::memory_order_relaxed);
    }

    PerfSnapshot snapshot() const {
        PerfSnapshot s;
        s.blocks = blocks_.load(std::memory_order_relaxed);
        s.overloads = overloads_.load(std::memory_order_relaxed);
        s.last_ns = last_.load(std::memory_order_relaxed);
        s.max_ns = max_.load(std::memory_order_relaxed);
        s.total_ns = total_.load(std::memory_order_relaxed);
        s.budget_ns = budget_ns_.load(std::memory_order_relaxed);
        return s;
    }

private:
    std::atomic<uint64_t> blocks_{0}, overloads_{0}, last_{0}, max_{0}, total_{0}, budget_ns_{0};
};

} // namespace mpcvst
