// SPDX-License-Identifier: MIT
#include "mpcvst/vst2_plugin.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>

namespace mpcvst {

namespace {

// The plug-in being opened: VSTPluginMain and MVST_OP_OPEN may call the host
// before the effect carries our pointer.
thread_local Vst2Plugin* g_opening = nullptr;
thread_local double g_opening_rate = 44100.0;
thread_local int g_opening_block = 256;

mvst_entry_fn entry_point_of(void* library) {
    static const char* const names[] = {"VSTPluginMain", "main_plugin", "main"};
    for (const char* name : names)
        if (void* sym = dlsym(library, name)) return reinterpret_cast<mvst_entry_fn>(sym);
    return nullptr;
}

const char* const kHostCanDo[] = {"sendVstEvents", "sendVstMidiEvent", "sendVstTimeInfo",
                                  "receiveVstEvents", "receiveVstMidiEvent", "sizeWindow",
                                  "supplyIdle", "startStopProcess", "shellCategory"};

} // namespace

const char* plugin_kind_name(PluginKind kind) {
    switch (kind) {
    case PluginKind::Instrument: return "instrument";
    case PluginKind::Effect: return "effect";
    default: return "unknown";
    }
}

std::unique_ptr<Vst2Plugin> Vst2Plugin::load(const std::string& path, double sample_rate, int block_size,
                                             const LoadLimits& limits, std::string& error) {
    error.clear();
    // RTLD_LOCAL: a plug-in's symbols never reach the host or other plug-ins.
    // RTLD_NOW: an unresolved symbol fails here, not in the audio callback.
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        const char* why = dlerror();
        error = std::string("dlopen failed: ") + (why ? why : "unknown error");
        return nullptr;
    }
    mvst_entry_fn entry = entry_point_of(library);
    if (!entry) {
        error = "no VST2 entry point (VSTPluginMain or main) exported";
        dlclose(library);
        return nullptr;
    }
    g_opening = nullptr;
    g_opening_rate = sample_rate;
    g_opening_block = block_size;
    mvst_effect* effect = entry(&Vst2Plugin::host_callback);
    if (!effect) {
        error = "entry point returned no effect";
        dlclose(library);
        return nullptr;
    }
    if (effect->magic != MVST_MAGIC) {
        error = "effect has the wrong magic number (not a VST2 plug-in)";
        dlclose(library);
        return nullptr;
    }
    if (!effect->dispatcher || !effect->get_parameter || !effect->set_parameter) {
        error = "effect lacks a dispatcher or parameter functions";
        dlclose(library);
        return nullptr;
    }
    auto reject = [&](const std::string& why) {
        error = why;
        // The effect was never opened: ask it to close anyway, which is what
        // hosts do when they give up before MVST_OP_OPEN. Then drop the library.
        dlclose(library);
        return std::unique_ptr<Vst2Plugin>();
    };
    if (effect->num_inputs < 0 || effect->num_inputs > limits.max_inputs)
        return reject("unsupported input count " + std::to_string(effect->num_inputs));
    if (effect->num_outputs < 0 || effect->num_outputs > limits.max_outputs)
        return reject("unsupported output count " + std::to_string(effect->num_outputs));
    if (effect->num_outputs == 0 && effect->num_inputs == 0)
        return reject("plug-in has no audio inputs or outputs");
    if (effect->num_params < 0 || effect->num_params > limits.max_params)
        return reject("unreasonable parameter count " + std::to_string(effect->num_params));
    if (effect->num_programs < 0 || effect->num_programs > limits.max_programs)
        return reject("unreasonable program count " + std::to_string(effect->num_programs));
    if (effect->initial_delay < 0 || effect->initial_delay > limits.max_initial_delay)
        return reject("unreasonable latency " + std::to_string(effect->initial_delay) + " frames");
    const bool replacing = (effect->flags & MVST_FLAG_CAN_REPLACING) != 0 && effect->process_replacing != nullptr;
    if (limits.require_replacing && !replacing)
        return reject("plug-in has no processReplacing (float processing)");

    std::unique_ptr<Vst2Plugin> plugin(new Vst2Plugin(library, effect, path));
    plugin->sample_rate_ = sample_rate;
    plugin->block_size_ = block_size;
    g_opening = plugin.get();
    plugin->dispatch(MVST_OP_OPEN);
    plugin->opened_ = true;
    plugin->dispatch(MVST_OP_SET_SAMPLE_RATE, 0, 0, nullptr, static_cast<float>(sample_rate));
    plugin->dispatch(MVST_OP_SET_BLOCK_SIZE, 0, block_size);
    g_opening = nullptr;
    plugin->read_info();
    return plugin;
}

Vst2Plugin::Vst2Plugin(void* library, mvst_effect* effect, std::string path)
    : library_(library), effect_(effect) {
    info_.path = std::move(path);
    effect_->resvd1 = reinterpret_cast<intptr_t>(this);
    time_.sample_rate = 44100.0;
    time_.tempo = 120.0;
    time_.time_sig_numerator = 4;
    time_.time_sig_denominator = 4;
    time_.flags = MVST_TIME_TEMPO_VALID | MVST_TIME_PPQ_POS_VALID | MVST_TIME_BARS_VALID | MVST_TIME_SIG_VALID;
}

Vst2Plugin::~Vst2Plugin() {
    if (active_) deactivate();
    if (opened_) dispatch(MVST_OP_CLOSE);
    effect_ = nullptr;
    // The caller guarantees the audio thread is no longer inside process():
    // unloading a library while a thread runs its code is a crash.
    dlclose(library_);
}

intptr_t Vst2Plugin::dispatch(int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt) const {
    return effect_->dispatcher(effect_, opcode, index, value, ptr, opt);
}

std::string Vst2Plugin::dispatch_string(int32_t opcode, int32_t index) const {
    char text[256] = {};
    dispatch(opcode, index, 0, text, 0.0f);
    text[sizeof(text) - 1] = 0;
    std::string s(text);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) ++start;
    return s.substr(start);
}

void Vst2Plugin::read_info() {
    info_.name = dispatch_string(MVST_OP_GET_EFFECT_NAME);
    info_.product = dispatch_string(MVST_OP_GET_PRODUCT_STRING);
    info_.vendor = dispatch_string(MVST_OP_GET_VENDOR_STRING);
    if (info_.name.empty()) info_.name = info_.product;
    if (info_.name.empty()) {
        size_t slash = info_.path.find_last_of('/');
        std::string file = slash == std::string::npos ? info_.path : info_.path.substr(slash + 1);
        size_t dot = file.rfind(".so");
        info_.name = dot == std::string::npos ? file : file.substr(0, dot);
    }
    info_.unique_id = effect_->unique_id;
    info_.version = effect_->version;
    info_.vst_version = static_cast<int32_t>(dispatch(MVST_OP_GET_VST_VERSION));
    info_.vendor_version = static_cast<int32_t>(dispatch(MVST_OP_GET_VENDOR_VERSION));
    info_.num_params = effect_->num_params;
    info_.num_programs = effect_->num_programs;
    info_.num_inputs = effect_->num_inputs;
    info_.num_outputs = effect_->num_outputs;
    info_.initial_delay = effect_->initial_delay;
    info_.flags = effect_->flags;
    info_.category = static_cast<int32_t>(dispatch(MVST_OP_GET_PLUG_CATEGORY));
    info_.is_synth = (effect_->flags & MVST_FLAG_IS_SYNTH) != 0;
    info_.has_chunks = (effect_->flags & MVST_FLAG_PROGRAM_CHUNKS) != 0;
    info_.can_replace = (effect_->flags & MVST_FLAG_CAN_REPLACING) != 0 && effect_->process_replacing;
    info_.can_double_replace = (effect_->flags & MVST_FLAG_CAN_DOUBLE_REPLACING) != 0 && effect_->process_double_replacing;
    info_.has_editor = (effect_->flags & MVST_FLAG_HAS_EDITOR) != 0;
    info_.receives_midi = info_.is_synth || can_do("receiveVstMidiEvent") || can_do("receiveVstEvents");
    if (info_.is_synth || info_.category == MVST_CATEGORY_SYNTH || info_.category == MVST_CATEGORY_GENERATOR)
        info_.kind = PluginKind::Instrument;
    else if (info_.num_inputs > 0 && info_.num_outputs > 0)
        info_.kind = PluginKind::Effect;
    else if (info_.num_inputs == 0 && info_.num_outputs > 0)
        info_.kind = PluginKind::Instrument;
    else
        info_.kind = PluginKind::Unknown;
}

bool Vst2Plugin::activate(double sample_rate, int block_size, int max_events, std::string& error) {
    if (block_size <= 0 || block_size > 65536) {
        error = "block size out of range";
        return false;
    }
    if (sample_rate < 8000.0 || sample_rate > 384000.0) {
        error = "sample rate out of range";
        return false;
    }
    if (active_) deactivate();
    sample_rate_ = sample_rate;
    block_size_ = block_size;
    max_events_ = std::max(1, max_events);

    const size_t ins = static_cast<size_t>(std::max(1, effect_->num_inputs));
    const size_t outs = static_cast<size_t>(std::max(1, effect_->num_outputs));
    audio_in_.assign(ins * static_cast<size_t>(block_size), 0.0f);
    audio_out_.assign(outs * static_cast<size_t>(block_size), 0.0f);
    in_ptrs_.resize(ins);
    out_ptrs_.resize(outs);
    for (size_t c = 0; c < ins; ++c) in_ptrs_[c] = audio_in_.data() + c * static_cast<size_t>(block_size);
    for (size_t c = 0; c < outs; ++c) out_ptrs_[c] = audio_out_.data() + c * static_cast<size_t>(block_size);
    midi_.assign(static_cast<size_t>(max_events_), mvst_midi_event{});
    events_blob_.assign(sizeof(mvst_events) + sizeof(mvst_event*) * static_cast<size_t>(max_events_), 0);
    events_ = reinterpret_cast<mvst_events*>(events_blob_.data());
    for (int i = 0; i < max_events_; ++i)
        events_->events[i] = reinterpret_cast<mvst_event*>(&midi_[static_cast<size_t>(i)]);

    time_.sample_rate = sample_rate;
    dispatch(MVST_OP_SET_SAMPLE_RATE, 0, 0, nullptr, static_cast<float>(sample_rate));
    dispatch(MVST_OP_SET_BLOCK_SIZE, 0, block_size);
    dispatch(MVST_OP_MAINS_CHANGED, 0, 1);
    dispatch(MVST_OP_START_PROCESS);
    active_ = true;
    return true;
}

void Vst2Plugin::deactivate() {
    if (!active_) return;
    dispatch(MVST_OP_STOP_PROCESS);
    dispatch(MVST_OP_MAINS_CHANGED, 0, 0);
    active_ = false;
    free_buffers();
}

void Vst2Plugin::free_buffers() {
    audio_in_.clear();
    audio_out_.clear();
    in_ptrs_.clear();
    out_ptrs_.clear();
    midi_.clear();
    events_blob_.clear();
    events_ = nullptr;
}

void Vst2Plugin::update_time(const TransportState& t, int frames) {
    (void)frames;
    time_.sample_pos = t.sample_pos;
    time_.sample_rate = sample_rate_;
    time_.ppq_pos = t.ppq_pos;
    time_.tempo = t.tempo > 0.0 ? t.tempo : 120.0;
    time_.bar_start_pos = t.bar_start_pos;
    time_.time_sig_numerator = t.time_sig_numerator > 0 ? t.time_sig_numerator : 4;
    time_.time_sig_denominator = t.time_sig_denominator > 0 ? t.time_sig_denominator : 4;
    int32_t flags = MVST_TIME_TEMPO_VALID | MVST_TIME_PPQ_POS_VALID | MVST_TIME_BARS_VALID | MVST_TIME_SIG_VALID;
    if (t.playing) flags |= MVST_TIME_TRANSPORT_PLAYING;
    if (t.playing != was_playing_) flags |= MVST_TIME_TRANSPORT_CHANGED;
    was_playing_ = t.playing;
    time_.flags = flags;
}

bool Vst2Plugin::process(const float* const* inputs, float* const* outputs, int frames,
                         const MidiEvent* events, int num_events, const TransportState& transport) {
    const int n_in = effect_->num_inputs, n_out = effect_->num_outputs;
    if (frames <= 0) return true;
    if (!active_ || frames > block_size_ || bypass_.load(std::memory_order_relaxed)) {
        // Silence, or for an effect its input passed through.
        for (int c = 0; c < n_out; ++c) {
            if (!outputs[c]) continue;
            if (info_.kind == PluginKind::Effect && c < n_in && inputs[c])
                std::memcpy(outputs[c], inputs[c], sizeof(float) * static_cast<size_t>(frames));
            else
                std::memset(outputs[c], 0, sizeof(float) * static_cast<size_t>(frames));
        }
        return false;
    }
    update_time(transport, frames);

    // Events first, as plug-ins expect: in deltaFrames order, none past the block.
    int count = 0;
    for (int i = 0; i < num_events && count < max_events_; ++i) {
        const MidiEvent& ev = events[i];
        if (ev.length == 0 || ev.length > 3 || ev.delta_frames >= frames) continue;
        mvst_midi_event& m = midi_[static_cast<size_t>(count++)];
        m.type = MVST_EVENT_MIDI;
        m.byte_size = sizeof(mvst_midi_event);
        m.delta_frames = ev.delta_frames < 0 ? 0 : ev.delta_frames;
        m.flags = MVST_MIDI_EVENT_IS_REALTIME;
        m.note_length = 0;
        m.note_offset = 0;
        m.midi_data[0] = ev.data[0];
        m.midi_data[1] = ev.length > 1 ? ev.data[1] : 0;
        m.midi_data[2] = ev.length > 2 ? ev.data[2] : 0;
        m.midi_data[3] = 0;
        m.detune = 0;
        m.note_off_velocity = 0;
        m.reserved1 = m.reserved2 = 0;
    }
    if (count > 0) {
        events_->num_events = count;
        events_->reserved = 0;
        dispatch(MVST_OP_PROCESS_EVENTS, 0, 0, events_);
    }

    for (int c = 0; c < n_in; ++c) {
        if (inputs && inputs[c])
            std::memcpy(in_ptrs_[static_cast<size_t>(c)], inputs[c], sizeof(float) * static_cast<size_t>(frames));
        else
            std::memset(in_ptrs_[static_cast<size_t>(c)], 0, sizeof(float) * static_cast<size_t>(frames));
    }
    // Replacing plug-ins may still read the output buffers: keep them clean.
    for (int c = 0; c < n_out; ++c)
        std::memset(out_ptrs_[static_cast<size_t>(c)], 0, sizeof(float) * static_cast<size_t>(frames));

    effect_->process_replacing(effect_, in_ptrs_.data(), out_ptrs_.data(), frames);

    uint64_t bad = 0;
    for (int c = 0; c < n_out; ++c) {
        float* src = out_ptrs_[static_cast<size_t>(c)];
        for (int i = 0; i < frames; ++i) {
            // A NaN or infinity must never reach the mix: zero it, count it.
            if (!(std::fabs(src[i]) <= 1.0e6f)) {
                src[i] = 0.0f;
                ++bad;
            }
        }
        if (outputs[c]) std::memcpy(outputs[c], src, sizeof(float) * static_cast<size_t>(frames));
    }
    if (bad) bad_samples_.fetch_add(bad, std::memory_order_relaxed);
    return true;
}

// ---- host callback ----

intptr_t Vst2Plugin::host_callback(mvst_effect* e, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt) {
    Vst2Plugin* self = nullptr;
    if (e && e->resvd1) self = reinterpret_cast<Vst2Plugin*>(e->resvd1);
    if (!self) self = g_opening;
    if (self) return self->answer(opcode, index, value, ptr, opt);

    // Before the effect exists: only what a plug-in asks in its entry point.
    switch (opcode) {
    case MVST_HOST_VERSION: return MVST_HOST_API_VERSION;
    case MVST_HOST_CURRENT_ID: return 0;
    case MVST_HOST_GET_SAMPLE_RATE: return static_cast<intptr_t>(g_opening_rate);
    case MVST_HOST_GET_BLOCK_SIZE: return g_opening_block;
    case MVST_HOST_GET_LANGUAGE: return 1;
    case MVST_HOST_GET_VENDOR_STRING: if (ptr) std::strcpy(static_cast<char*>(ptr), "MPC Sample VST Host"); return 1;
    case MVST_HOST_GET_PRODUCT_STRING: if (ptr) std::strcpy(static_cast<char*>(ptr), "mpcvst"); return 1;
    case MVST_HOST_GET_VENDOR_VERSION: return 1;
    case MVST_HOST_CAN_DO: {
        const char* asked = static_cast<const char*>(ptr);
        if (asked) for (const char* yes : kHostCanDo) if (std::strcmp(asked, yes) == 0) return 1;
        return 0;
    }
    default: return 0;
    }
}

intptr_t Vst2Plugin::answer(int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt) {
    (void)index; (void)value; (void)opt;
    callbacks_.calls.fetch_add(1, std::memory_order_relaxed);
    switch (opcode) {
    case MVST_HOST_AUTOMATE: callbacks_.automate.fetch_add(1, std::memory_order_relaxed); return 0;
    case MVST_HOST_VERSION: return MVST_HOST_API_VERSION;
    case MVST_HOST_CURRENT_ID: return effect_ ? effect_->unique_id : 0;
    case MVST_HOST_IDLE: return 0;
    case MVST_HOST_PIN_CONNECTED: return 0; // every pin is connected
    case MVST_HOST_WANT_MIDI: return 1;
    case MVST_HOST_GET_TIME: return reinterpret_cast<intptr_t>(&time_);
    case MVST_HOST_PROCESS_EVENTS: return 0; // MIDI out is not routed anywhere yet
    case MVST_HOST_TEMPO_AT: return static_cast<intptr_t>(time_.tempo * 10000.0);
    case MVST_HOST_IO_CHANGED: callbacks_.io_changed.fetch_add(1, std::memory_order_relaxed); return 0;
    case MVST_HOST_SIZE_WINDOW: return 0;
    case MVST_HOST_GET_SAMPLE_RATE: return static_cast<intptr_t>(sample_rate_);
    case MVST_HOST_GET_BLOCK_SIZE: return block_size_;
    case MVST_HOST_GET_INPUT_LATENCY: return 0;
    case MVST_HOST_GET_OUTPUT_LATENCY: return 0;
    case MVST_HOST_GET_CURRENT_PROCESS_LEVEL: return active_ ? MVST_PROCESS_LEVEL_REALTIME : MVST_PROCESS_LEVEL_USER;
    case MVST_HOST_GET_AUTOMATION_STATE: return 1; // off
    case MVST_HOST_GET_VENDOR_STRING: if (ptr) std::strcpy(static_cast<char*>(ptr), "MPC Sample VST Host"); return 1;
    case MVST_HOST_GET_PRODUCT_STRING: if (ptr) std::strcpy(static_cast<char*>(ptr), "mpcvst"); return 1;
    case MVST_HOST_GET_VENDOR_VERSION: return 1;
    case MVST_HOST_VENDOR_SPECIFIC: return 0;
    case MVST_HOST_CAN_DO: {
        const char* asked = static_cast<const char*>(ptr);
        if (asked) for (const char* yes : kHostCanDo) if (std::strcmp(asked, yes) == 0) return 1;
        return 0;
    }
    case MVST_HOST_GET_LANGUAGE: return 1; // English
    case MVST_HOST_GET_DIRECTORY: return 0;
    case MVST_HOST_UPDATE_DISPLAY: return 0;
    case MVST_HOST_BEGIN_EDIT: return 0;
    case MVST_HOST_END_EDIT: return 0;
    default:
        callbacks_.unanswered.fetch_add(1, std::memory_order_relaxed);
        callbacks_.last_unanswered.store(opcode, std::memory_order_relaxed);
        return 0;
    }
}

// ---- parameters ----

float Vst2Plugin::get_param(int index) const {
    if (index < 0 || index >= info_.num_params) return 0.0f;
    float v = effect_->get_parameter(effect_, index);
    if (!(v >= 0.0f && v <= 1.0f)) v = v < 0.0f || v != v ? 0.0f : 1.0f;
    return v;
}

void Vst2Plugin::set_param(int index, float value) {
    if (index < 0 || index >= info_.num_params) return;
    if (!(value >= 0.0f)) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    effect_->set_parameter(effect_, index, value);
}

std::string Vst2Plugin::param_name(int index) const {
    if (index < 0 || index >= info_.num_params) return {};
    return dispatch_string(MVST_OP_GET_PARAM_NAME, index);
}

std::string Vst2Plugin::param_label(int index) const {
    if (index < 0 || index >= info_.num_params) return {};
    return dispatch_string(MVST_OP_GET_PARAM_LABEL, index);
}

std::string Vst2Plugin::param_display(int index) const {
    if (index < 0 || index >= info_.num_params) return {};
    return dispatch_string(MVST_OP_GET_PARAM_DISPLAY, index);
}

bool Vst2Plugin::param_automatable(int index) const {
    if (index < 0 || index >= info_.num_params) return false;
    return dispatch(MVST_OP_CAN_BE_AUTOMATED, index) != 0;
}

// ---- programs ----

int Vst2Plugin::current_program() const {
    if (info_.num_programs <= 0) return 0;
    intptr_t p = dispatch(MVST_OP_GET_PROGRAM);
    if (p < 0 || p >= info_.num_programs) return 0;
    return static_cast<int>(p);
}

void Vst2Plugin::set_program(int index) {
    if (index < 0 || index >= info_.num_programs) return;
    dispatch(MVST_OP_BEGIN_SET_PROGRAM);
    dispatch(MVST_OP_SET_PROGRAM, 0, index);
    dispatch(MVST_OP_END_SET_PROGRAM);
}

std::string Vst2Plugin::program_name(int index) const {
    if (index < 0 || index >= info_.num_programs) return {};
    char text[256] = {};
    if (dispatch(MVST_OP_GET_PROGRAM_NAME_INDEXED, index, -1, text) == 0 && index == current_program())
        dispatch(MVST_OP_GET_PROGRAM_NAME, 0, 0, text);
    text[sizeof(text) - 1] = 0;
    return text;
}

// ---- state ----

bool Vst2Plugin::get_chunk(std::vector<uint8_t>& out) const {
    out.clear();
    if (!info_.has_chunks) return false;
    void* data = nullptr;
    intptr_t size = dispatch(MVST_OP_GET_CHUNK, 0, 0, &data);
    if (!data || size <= 0 || static_cast<size_t>(size) > (64u << 20)) return false;
    out.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    return true;
}

bool Vst2Plugin::set_chunk(const uint8_t* data, size_t size) {
    if (!info_.has_chunks || !data || size == 0 || size > (64u << 20)) return false;
    // The plug-in may keep the pointer only for the call; it must copy.
    dispatch(MVST_OP_SET_CHUNK, 0, static_cast<intptr_t>(size), const_cast<uint8_t*>(data));
    return true;
}

int Vst2Plugin::tail_size() const {
    intptr_t t = dispatch(MVST_OP_GET_TAIL_SIZE);
    return t > 1 && t < (1 << 24) ? static_cast<int>(t) : 0;
}

bool Vst2Plugin::can_do(const char* what) const {
    return dispatch(MVST_OP_CAN_DO, 0, 0, const_cast<char*>(what)) > 0;
}

} // namespace mpcvst
