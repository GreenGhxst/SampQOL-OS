// SPDX-License-Identifier: MIT
// Tests 5-11 of the specification: entry-point validation, lifecycle, MIDI
// note on/off, buffer bounds, parameters, preset serialization, bypass and
// unload. Plus the NaN guard and the offline renderer's determinism.
#include "check.h"
#include "test_paths.h"

#include "mpcvst/offline_render.h"
#include "mpcvst/plugin_chain.h"
#include "mpcvst/state_file.h"
#include "mpcvst/vst2_plugin.h"

#include <cmath>
#include <cstdlib>
#include <sys/stat.h>

using namespace mpcvst;

static double rms(const std::vector<float>& v, size_t from = 0, size_t to = 0) {
    if (to == 0 || to > v.size()) to = v.size();
    double s = 0;
    for (size_t i = from; i < to; ++i) s += static_cast<double>(v[i]) * v[i];
    return to > from ? std::sqrt(s / static_cast<double>(to - from)) : 0.0;
}

int main() {
    LoadLimits limits;
    std::string error;

    // --- 5. entry-point validation on the valid plug-ins
    auto synth = Vst2Plugin::load(plugin_path("mock_instrument.so"), 44100.0, 128, limits, error);
    REQUIRE(synth != nullptr);
    CHECK_MSG(error.empty(), error);
    CHECK(synth->info().name == "Mock Instrument");
    CHECK(synth->info().vendor == "MPC Sample VST Host project");
    CHECK(synth->info().kind == PluginKind::Instrument);
    CHECK(synth->info().is_synth);
    CHECK(synth->info().has_chunks);
    CHECK(synth->info().receives_midi);
    CHECK(synth->info().num_params == 4);
    CHECK(synth->info().num_programs == 4);
    CHECK(synth->info().num_inputs == 0);
    CHECK(synth->info().num_outputs == 2);
    CHECK(synth->info().vst_version == 2400);

    auto fx = Vst2Plugin::load(plugin_path("mock_effect.so"), 44100.0, 128, limits, error);
    REQUIRE(fx != nullptr);
    CHECK(fx->info().kind == PluginKind::Effect);
    CHECK(!fx->info().has_chunks);
    CHECK(fx->info().num_inputs == 2 && fx->info().num_outputs == 2);

    // Invalid metadata and entry points are rejected without a crash.
    CHECK(Vst2Plugin::load(plugin_path("bad_no_entry.so"), 44100.0, 128, limits, error) == nullptr);
    CHECK_MSG(error.find("entry point") != std::string::npos, error);
    CHECK(Vst2Plugin::load(plugin_path("bad_wrong_magic.so"), 44100.0, 128, limits, error) == nullptr);
    CHECK_MSG(error.find("magic") != std::string::npos, error);
    CHECK(Vst2Plugin::load(plugin_path("bad_huge_io.so"), 44100.0, 128, limits, error) == nullptr);
    CHECK_MSG(error.find("output count") != std::string::npos || error.find("input count") != std::string::npos, error);
    CHECK(Vst2Plugin::load(plugin_path("bad_no_replacing.so"), 44100.0, 128, limits, error) == nullptr);
    CHECK_MSG(error.find("processReplacing") != std::string::npos, error);
    CHECK(Vst2Plugin::load(plugin_path("does_not_exist.so"), 44100.0, 128, limits, error) == nullptr);
    CHECK_MSG(error.find("dlopen") != std::string::npos, error);

    // --- 6. lifecycle: activate, process, deactivate, re-activate at another block size
    CHECK(!synth->active());
    REQUIRE(synth->activate(44100.0, 128, 64, error));
    CHECK(synth->active());
    CHECK(synth->block_size() == 128);
    synth->deactivate();
    CHECK(!synth->active());
    REQUIRE(synth->activate(48000.0, 256, 64, error));
    CHECK(synth->sample_rate() == 48000.0 && synth->block_size() == 256);
    synth->deactivate();

    // --- 7. MIDI note on/off through the chain, with the offline renderer
    EngineConfig cfg;
    cfg.sample_rate = 44100.0;
    cfg.block_size = 128;
    {
        PluginChain chain(cfg);
        auto s = Vst2Plugin::load(plugin_path("mock_instrument.so"), cfg.sample_rate, cfg.block_size, limits, error);
        REQUIRE(s != nullptr);
        REQUIRE(chain.set_instrument(std::move(s), error));
        RenderOptions opt;
        opt.seconds = 1.0;
        REQUIRE(parse_midi_script("0.25 on 1 60 100\n0.5 off 1 60\n", opt.midi, error));
        CHECK(opt.midi.size() == 2);
        RenderResult r = render_offline(chain, opt);
        CHECK(r.left.size() == 44100);
        CHECK(r.bad_samples == 0);
        CHECK(r.midi_dropped == 0);
        // Silence before the note, sound after it, decay after the release.
        const size_t on = static_cast<size_t>(0.25 * 44100), off = static_cast<size_t>(0.5 * 44100);
        CHECK_MSG(rms(r.left, 0, on) == 0.0, "silence before note on");
        CHECK_MSG(rms(r.left, on + 100, on + 4000) > 0.05, "sound after note on: " + std::to_string(rms(r.left, on + 100, on + 4000)));
        CHECK_MSG(rms(r.left, off + 20000, r.left.size()) < 0.01, "decayed after note off: " + std::to_string(rms(r.left, off + 20000, r.left.size())));
        CHECK(r.peak <= 1.0f);
        // Stereo copy from the mock: both channels identical.
        bool same = true;
        for (size_t i = 0; i < r.left.size(); i += 97) same = same && r.left[i] == r.right[i];
        CHECK(same);
        // The plug-in read the transport.
        CHECK(r.perf.blocks == (44100 + 127) / 128);
        chain.clear();
    }

    // Determinism across block sizes: the same script renders the same samples.
    {
        std::vector<float> a, b;
        for (int block : {128, 64}) {
            EngineConfig c = cfg;
            c.block_size = block;
            PluginChain chain(c);
            auto s = Vst2Plugin::load(plugin_path("mock_instrument.so"), c.sample_rate, c.block_size, limits, error);
            REQUIRE(s != nullptr);
            REQUIRE(chain.set_instrument(std::move(s), error));
            RenderOptions opt;
            opt.seconds = 0.5;
            REQUIRE(parse_midi_script("0 on 1 48 90\n0.1 on 1 55 90\n0.3 off 1 48\n0.3 off 1 55\n", opt.midi, error));
            RenderResult r = render_offline(chain, opt);
            (block == 128 ? a : b) = r.left;
            chain.clear();
        }
        REQUIRE(a.size() == b.size());
        double diff = 0;
        for (size_t i = 0; i < a.size(); ++i) diff += std::fabs(a[i] - b[i]);
        CHECK_MSG(diff == 0.0, "samples identical across block sizes, diff " + std::to_string(diff));
    }

    // --- 8. audio buffer bounds: a block shorter than block_size, and
    // frames > block_size are refused (silence, false).
    {
        auto s = Vst2Plugin::load(plugin_path("mock_instrument.so"), 44100.0, 128, limits, error);
        REQUIRE(s != nullptr);
        REQUIRE(s->activate(44100.0, 128, 16, error));
        std::vector<float> l(256, 12345.0f), r(256, 12345.0f);
        float* outs[2] = {l.data(), r.data()};
        TransportState t;
        MidiEvent on; on.data[0] = 0x90; on.data[1] = 60; on.data[2] = 127; on.length = 3; on.delta_frames = 0;
        CHECK(s->process(nullptr, outs, 100, &on, 1, t));
        // Frames 100..255 untouched: the host never writes past `frames`.
        bool untouched = true;
        for (size_t i = 100; i < 256; ++i) untouched = untouched && l[i] == 12345.0f && r[i] == 12345.0f;
        CHECK_MSG(untouched, "no write past frames");
        CHECK(!s->process(nullptr, outs, 129, nullptr, 0, t)); // more than the block: refused, silence
        bool silent = true;
        for (size_t i = 0; i < 129; ++i) silent = silent && l[i] == 0.0f;
        CHECK(silent);
        // Events past the block are dropped, not delivered out of range.
        MidiEvent late = on; late.delta_frames = 500;
        CHECK(s->process(nullptr, outs, 128, &late, 1, t));
        // A nullptr output channel is allowed (discarded).
        float* one[2] = {l.data(), nullptr};
        CHECK(s->process(nullptr, one, 128, nullptr, 0, t));
        // Too many events: only max_events are delivered (16 here), no overflow.
        std::vector<MidiEvent> many(100, on);
        CHECK(s->process(nullptr, outs, 128, many.data(), 100, t));
        s->deactivate();
    }

    // --- 9. parameter updates
    {
        CHECK(synth->param_name(0) == "Volume");
        CHECK(synth->param_name(1) == "Wave");
        CHECK(synth->param_label(2) == "s");
        synth->set_param(0, 0.25f);
        CHECK(std::fabs(synth->get_param(0) - 0.25f) < 1e-6f);
        synth->set_param(0, 7.0f);  // clamped by the host
        CHECK(synth->get_param(0) == 1.0f);
        synth->set_param(0, -3.0f);
        CHECK(synth->get_param(0) == 0.0f);
        synth->set_param(99, 0.5f); // out of range: ignored
        CHECK(synth->get_param(99) == 0.0f);
        CHECK(synth->param_automatable(0));
        synth->set_param(1, 0.5f);
        CHECK(synth->param_display(1) == "saw");
        // Programs
        CHECK(synth->program_name(0) == "Sine");
        CHECK(synth->program_name(2) == "Square");
        synth->set_program(2);
        CHECK(synth->current_program() == 2);
        CHECK(synth->get_param(1) == 1.0f); // Square program's wave
        synth->set_program(99); // ignored
        CHECK(synth->current_program() == 2);
    }

    // --- 10. preset serialization: chunk round trip and parameter round trip
    {
        synth->set_program(1);
        synth->set_param(0, 0.33f);
        synth->set_param(3, 0.77f);
        PluginState st;
        capture_plugin_state(*synth, st);
        CHECK(st.body_kind == StateBodyKind::Chunk);
        CHECK(st.unique_id == synth->info().unique_id);
        CHECK(st.program == 1);
        st.pad = 5;
        st.midi_channel = 1;
        st.resource_files.push_back("Samples/x.wav");
        st.extra.emplace_back("knob1", "0");
        std::vector<uint8_t> bytes = encode_state(st);
        PluginState back;
        REQUIRE(decode_state(bytes.data(), bytes.size(), back, error));
        CHECK(back.pad == 5 && back.midi_channel == 1 && back.program == 1);
        CHECK(back.chunk == st.chunk);
        CHECK(back.resource_files.size() == 1 && back.resource_files[0] == "Samples/x.wav");
        CHECK(back.extra.size() == 1 && back.extra[0].first == "knob1");
        // Damage: a flipped byte fails the CRC, a truncated file fails cleanly.
        std::vector<uint8_t> damaged = bytes;
        damaged[20] ^= 0x55;
        CHECK(!decode_state(damaged.data(), damaged.size(), back, error));
        CHECK(!decode_state(bytes.data(), bytes.size() - 7, back, error));
        CHECK(!decode_state(bytes.data(), 3, back, error));
        // Change the plug-in, restore, compare.
        synth->set_program(0);
        synth->set_param(0, 0.9f);
        synth->set_param(3, 0.1f);
        REQUIRE(apply_plugin_state(*synth, st, error));
        CHECK(synth->current_program() == 1);
        CHECK(std::fabs(synth->get_param(0) - 0.33f) < 1e-6f);
        CHECK(std::fabs(synth->get_param(3) - 0.77f) < 1e-6f);
        // Wrong identity is refused.
        PluginState other = st;
        other.unique_id = 12345;
        CHECK(!apply_plugin_state(*synth, other, error));
        // File round trip.
        mkdir(temp_dir().c_str(), 0755);
        std::string file = temp_dir() + "/state_test.mvxs";
        REQUIRE(save_state_file(file, st, error));
        PluginState loaded;
        REQUIRE(load_state_file(file, loaded, error));
        CHECK(loaded.chunk == st.chunk && loaded.plugin_name == "Mock Instrument");
        // Parameter-only state (the effect).
        fx->set_param(0, 0.1f);
        fx->set_param(1, 0.2f);
        PluginState fs;
        capture_plugin_state(*fx, fs);
        CHECK(fs.body_kind == StateBodyKind::Parameters && fs.params.size() == 2);
        fx->set_param(0, 0.9f);
        REQUIRE(apply_plugin_state(*fx, fs, error));
        CHECK(std::fabs(fx->get_param(0) - 0.1f) < 1e-6f);
        // A missing plug-in keeps its state file readable (recovery): the
        // record says which plug-in, nothing is deleted.
        PluginState orphan;
        REQUIRE(load_state_file(file, orphan, error));
        CHECK(orphan.plugin_path == synth->info().path);
    }

    // --- 11. bypass and unload
    {
        PluginChain chain(cfg);
        auto s = Vst2Plugin::load(plugin_path("mock_instrument.so"), cfg.sample_rate, cfg.block_size, limits, error);
        auto e = Vst2Plugin::load(plugin_path("mock_effect.so"), cfg.sample_rate, cfg.block_size, limits, error);
        REQUIRE(s && e);
        Vst2Plugin* synth_raw = s.get();
        Vst2Plugin* fx_raw = e.get();
        REQUIRE(chain.set_instrument(std::move(s), error));
        REQUIRE(chain.add_effect(std::move(e), error));
        CHECK(chain.plugin_count() == 2);
        RenderOptions opt;
        opt.seconds = 0.3;
        REQUIRE(parse_midi_script("0 on 1 60 100\n", opt.midi, error));
        RenderResult loud = render_offline(chain, opt);
        CHECK(rms(loud.left) > 0.02);
        // Effect bypass: passes the instrument through unchanged.
        fx_raw->set_bypass(true);
        RenderResult passed = render_offline(chain, opt);
        CHECK(rms(passed.left) > 0.02);
        fx_raw->set_bypass(false);
        // Instrument bypass: silence.
        synth_raw->set_bypass(true);
        RenderResult quiet = render_offline(chain, opt);
        CHECK_MSG(rms(quiet.left) < 1e-4, "only the effect's tail remains: " + std::to_string(rms(quiet.left)));
        synth_raw->set_bypass(false);
        // Whole-chain emergency bypass.
        chain.set_bypass_all(true);
        RenderResult off = render_offline(chain, opt);
        CHECK(rms(off.left) == 0.0 && off.perf.blocks > 0);
        chain.set_bypass_all(false);
        // Effect with a low cutoff and gain change does change the sound.
        fx_raw->set_param(1, 0.0f); // 50 Hz low-pass
        RenderResult dull = render_offline(chain, opt);
        CHECK_MSG(rms(dull.left) < rms(loud.left) * 0.5, "low-pass reduced level: " + std::to_string(rms(dull.left)) + " vs " + std::to_string(rms(loud.left)));
        // Unload: clear() deactivates and dlcloses; nothing crashes after.
        chain.clear();
        CHECK(chain.plugin_count() == 0);
        RenderResult empty = render_offline(chain, opt);
        CHECK(rms(empty.left) == 0.0);
    }

    // --- NaN guard: a plug-in writing NaN/inf never reaches the output.
    {
        auto bad = Vst2Plugin::load(plugin_path("bad_nan_output.so"), 44100.0, 128, limits, error);
        REQUIRE(bad != nullptr);
        REQUIRE(bad->activate(44100.0, 128, 8, error));
        std::vector<float> l(128), r(128);
        float* outs[2] = {l.data(), r.data()};
        TransportState t;
        CHECK(bad->process(nullptr, outs, 128, nullptr, 0, t));
        bool finite = true;
        for (float v : l) finite = finite && v == 0.0f;
        for (float v : r) finite = finite && v == 0.0f;
        CHECK(finite);
        CHECK(bad->bad_samples() == 256);
        bad->deactivate();
    }

    // Host callback: the mock asked for the time each block, and nothing it
    // asked went unanswered.
    CHECK(synth->callback_stats().unanswered.load() == 0);

    // Destroy explicitly before the summary (unload path).
    fx.reset();
    synth.reset();
    return check::summary("test_host_core");
}
