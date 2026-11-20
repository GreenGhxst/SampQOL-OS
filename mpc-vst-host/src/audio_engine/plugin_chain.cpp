// SPDX-License-Identifier: MIT
#include "mpcvst/plugin_chain.h"

#include <algorithm>
#include <cstring>

namespace mpcvst {

PluginChain::PluginChain(EngineConfig config)
    : config_(config), channels_(static_cast<size_t>(std::max(2, config.max_channels))) {
    events_.resize(static_cast<size_t>(config_.max_events_per_block));
    const size_t n = channels_ * static_cast<size_t>(config_.block_size);
    bus_a_.assign(n, 0.0f);
    bus_b_.assign(n, 0.0f);
    ptr_a_.resize(channels_);
    ptr_b_.resize(channels_);
    const_ptrs_.resize(channels_);
    for (size_t c = 0; c < channels_; ++c) {
        ptr_a_[c] = bus_a_.data() + c * static_cast<size_t>(config_.block_size);
        ptr_b_[c] = bus_b_.data() + c * static_cast<size_t>(config_.block_size);
    }
    transport_.configure(config_.sample_rate);
    perf_.configure(config_.sample_rate, config_.block_size);
}

PluginChain::~PluginChain() { clear(); }

bool PluginChain::set_instrument(std::unique_ptr<Vst2Plugin> plugin, std::string& error) {
    if (!plugin) { error = "no plug-in"; return false; }
    if (plugin->info().num_outputs > static_cast<int>(channels_)) {
        error = "instrument has more outputs than the chain carries";
        return false;
    }
    if (!plugin->activate(config_.sample_rate, config_.block_size, config_.max_events_per_block, error)) return false;
    instrument_ = std::move(plugin);
    return true;
}

bool PluginChain::add_effect(std::unique_ptr<Vst2Plugin> plugin, std::string& error) {
    if (!plugin) { error = "no plug-in"; return false; }
    if (plugin_count() >= static_cast<size_t>(config_.max_plugins)) { error = "plug-in limit reached"; return false; }
    if (plugin->info().num_inputs > static_cast<int>(channels_) || plugin->info().num_outputs > static_cast<int>(channels_)) {
        error = "effect has more channels than the chain carries";
        return false;
    }
    if (!plugin->activate(config_.sample_rate, config_.block_size, config_.max_events_per_block, error)) return false;
    effects_.push_back(std::move(plugin));
    return true;
}

void PluginChain::clear() {
    // Deactivate in reverse order, then destroy (dlclose) once nothing can
    // be inside their code: the caller has stopped the audio thread.
    for (auto it = effects_.rbegin(); it != effects_.rend(); ++it) (*it)->deactivate();
    if (instrument_) instrument_->deactivate();
    effects_.clear();
    instrument_.reset();
}

void PluginChain::all_notes_off() {
    for (uint8_t ch = 0; ch < 16; ++ch) {
        midi_.push(0, static_cast<uint8_t>(0xB0 | ch), 123, 0); // all notes off
        midi_.push(0, static_cast<uint8_t>(0xB0 | ch), 64, 0);  // sustain off
    }
}

void PluginChain::process(const float* in_left, const float* in_right, float* out_left, float* out_right, int frames) {
    if (frames <= 0) return;
    if (frames > config_.block_size) frames = config_.block_size;
    const uint64_t t0 = perf_.begin();
    const TransportState transport = transport_.state();
    const int n_events = midi_.drain(transport_.frame(), frames, events_.data(), config_.max_events_per_block);

    float** cur = ptr_a_.data();
    float** next = ptr_b_.data();
    const size_t bytes = sizeof(float) * static_cast<size_t>(frames);

    if (bypass_all_.load(std::memory_order_relaxed)) {
        if (out_left) std::memset(out_left, 0, bytes);
        if (out_right) std::memset(out_right, 0, bytes);
        transport_.advance(frames);
        perf_.end(t0);
        return;
    }

    // Stage 0: the input, or silence.
    if (in_left) std::memcpy(cur[0], in_left, bytes); else std::memset(cur[0], 0, bytes);
    if (in_right) std::memcpy(cur[1], in_right, bytes); else if (in_left) std::memcpy(cur[1], in_left, bytes); else std::memset(cur[1], 0, bytes);
    for (size_t c = 2; c < channels_; ++c) std::memset(cur[c], 0, bytes);

    // Stage 1: the instrument replaces the bus with its output (mono: both sides).
    if (instrument_) {
        for (size_t c = 0; c < channels_; ++c) const_ptrs_[c] = cur[c];
        instrument_->process(const_ptrs_.data(), next, frames, events_.data(), n_events, transport);
        const int outs = instrument_->info().num_outputs;
        if (outs == 1) std::memcpy(next[1], next[0], bytes);
        for (size_t c = static_cast<size_t>(std::max(outs, 2)); c < channels_; ++c) std::memset(next[c], 0, bytes);
        std::swap(cur, next);
    }

    // Stage 2: effects in order, each from cur into next.
    for (auto& fx : effects_) {
        for (size_t c = 0; c < channels_; ++c) const_ptrs_[c] = cur[c];
        fx->process(const_ptrs_.data(), next, frames, nullptr, 0, transport);
        const int outs = fx->info().num_outputs;
        if (outs == 1) std::memcpy(next[1], next[0], bytes);
        std::swap(cur, next);
    }

    if (out_left) std::memcpy(out_left, cur[0], bytes);
    if (out_right) std::memcpy(out_right, cur[1], bytes);
    transport_.advance(frames);
    perf_.end(t0);
}

} // namespace mpcvst
