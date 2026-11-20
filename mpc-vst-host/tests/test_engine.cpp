// SPDX-License-Identifier: MIT
// The audio engine adapter on its own: the MIDI queue's ordering and
// timing, the transport, the performance monitor, the MIDI script parser
// and the WAV writer/reader.
#include "check.h"
#include "test_paths.h"

#include "mpcvst/midi_queue.h"
#include "mpcvst/offline_render.h"
#include "mpcvst/perf_monitor.h"
#include "mpcvst/transport.h"

#include <cmath>
#include <sys/stat.h>

using namespace mpcvst;

int main() {
    // --- MIDI queue: events fall in the right block with the right delta,
    // ordered by frame then arrival; late events play at frame 0.
    {
        MidiQueue<16> q;
        CHECK(q.push(100, 0x90, 60, 100));
        CHECK(q.push(100, 0x90, 64, 100));   // same frame, later arrival
        CHECK(q.push(130, 0x80, 60, 0));
        CHECK(q.push(300, 0x90, 67, 100));   // next block
        CHECK(q.push(50, 0xB0, 64, 127));    // earlier time, pushed later: still delivered in this block
        CHECK(q.pending() == 5);
        MidiEvent out[16];
        int n = q.drain(0, 128, out, 16);
        CHECK_MSG(n == 3, "events in block 0: " + std::to_string(n));
        // Sorted by delta then arrival: cc@50 (pushed last), note 60@100, note 64@100.
        CHECK(out[0].delta_frames == 50 && out[0].data[0] == 0xB0);
        CHECK(out[1].delta_frames == 100 && out[1].data[1] == 60);
        CHECK(out[2].delta_frames == 100 && out[2].data[1] == 64);
        CHECK(q.pending() == 2);
        n = q.drain(128, 128, out, 16);
        CHECK(n == 1 && out[0].delta_frames == 2 && out[0].data[0] == 0x80);
        n = q.drain(256, 128, out, 16);
        CHECK(n == 1 && out[0].delta_frames == 44 && out[0].data[1] == 67);
        CHECK(q.pending() == 0);
        // Kept events keep their arrival order across many drains.
        for (int i = 0; i < 8; ++i) q.push(static_cast<uint64_t>(1000 + (7 - i)), 0x90, static_cast<uint8_t>(i), 1);
        q.push(5, 0x90, 99, 1);
        n = q.drain(0, 128, out, 16);
        CHECK(n == 1 && out[0].data[1] == 99 && q.pending() == 8);
        n = q.drain(896, 128, out, 16);
        CHECK(n == 8);
        for (int i = 0; i < 8; ++i) CHECK(out[i].delta_frames == 104 + i && out[i].data[1] == 7 - i);
        // Full queue drops and counts.
        for (int i = 0; i < 20; ++i) q.push(0, 0x90, static_cast<uint8_t>(i), 1);
        CHECK(q.pending() == 16);
        CHECK(q.dropped() == 4);
        // max_out limits a drain without losing the rest.
        n = q.drain(0, 128, out, 10);
        CHECK(n == 10);
        n = q.drain(0, 128, out, 10);
        CHECK(n == 6);
        // 2-byte messages
        CHECK(q.push(0, 0xC0, 3, 0));
        n = q.drain(0, 1, out, 2);
        CHECK(n == 1 && out[0].length == 2);
        // bad lengths refused
        uint8_t b[3] = {0x90, 1, 1};
        CHECK(!q.push(0, b, 0));
        CHECK(!q.push(0, b, 4));
    }

    // --- Transport
    {
        Transport t;
        t.configure(44100.0);
        t.set_tempo(120.0);
        t.set_playing(true);
        TransportState s = t.state();
        CHECK(s.playing && s.sample_pos == 0.0 && s.ppq_pos == 0.0 && s.bar_start_pos == 0.0);
        for (int i = 0; i < 345; ++i) t.advance(128); // 44160 frames ~ 1.0014 s = 2.0027 beats
        s = t.state();
        CHECK(std::fabs(s.sample_pos - 44160.0) < 1e-9);
        CHECK(std::fabs(s.ppq_pos - 44160.0 * 2.0 / 44100.0) < 1e-9);
        CHECK(s.bar_start_pos == 0.0);
        for (int i = 0; i < 400; ++i) t.advance(128); // past bar 1 (4 beats = 88200 frames)
        s = t.state();
        CHECK(std::fabs(s.bar_start_pos - 4.0) < 1e-9);
        t.set_playing(false);
        double pos = t.state().sample_pos;
        t.advance(128);
        CHECK(t.state().sample_pos == pos); // stopped: no advance
        t.rewind();
        t.advance(0);
        CHECK(t.state().sample_pos == 0.0);
        t.set_tempo(0.0); // invalid: back to 120
        CHECK(t.state().tempo == 120.0);
        t.set_time_signature(3, 4);
        CHECK(t.state().time_sig_numerator == 3);
    }

    // --- PerfMonitor
    {
        PerfMonitor m;
        m.configure(44100.0, 128);
        PerfSnapshot s = m.snapshot();
        CHECK(s.budget_ns == static_cast<uint64_t>(1.0e9 * 128 / 44100.0));
        for (int i = 0; i < 10; ++i) { uint64_t t0 = m.begin(); m.end(t0); }
        s = m.snapshot();
        CHECK(s.blocks == 10 && s.overloads == 0 && s.max_ns >= s.last_ns);
        // A fake slow block is counted as an overload.
        uint64_t long_ago = monotonic_ns() - 10 * s.budget_ns;
        m.end(long_ago);
        s = m.snapshot();
        CHECK(s.overloads == 1 && s.max_ns >= 10 * s.budget_ns);
        CHECK(s.peak_percent() >= 1000.0);
        m.reset();
        CHECK(m.snapshot().blocks == 0);
    }

    // --- MIDI script parser
    {
        std::vector<ScriptEvent> ev;
        std::string error;
        CHECK(parse_midi_script("# comment\n0.5 on 1 60 100 # trailing\n0.25 off 2 61\n1 cc 1 74 64\n1 bend 1 -8192\n1 pc 3 5\n2 sustain 1 on\n3 allnotesoff\n", ev, error));
        CHECK_MSG(ev.size() == 6 + 16, std::to_string(ev.size())); // six events plus one per channel for allnotesoff
        CHECK(ev[0].seconds == 0.25 && ev[0].data[0] == 0x81 && ev[0].data[2] == 64); // sorted, default off velocity
        CHECK(ev[1].data[0] == 0x90 && ev[1].data[1] == 60 && ev[1].data[2] == 100);
        CHECK(ev[3].data[0] == 0xE0 && ev[3].data[1] == 0 && ev[3].data[2] == 0);
        CHECK(ev[4].data[0] == 0xC2 && ev[4].length == 2);
        CHECK(ev[5].data[1] == 64 && ev[5].data[2] == 127);
        CHECK(!parse_midi_script("0 on 17 60 100\n", ev, error));
        CHECK(!parse_midi_script("0 bogus 1\n", ev, error));
        CHECK(!parse_midi_script("-1 on 1 60 100\n", ev, error));
    }

    // --- WAV round trips (16-bit and float)
    {
        mkdir(temp_dir().c_str(), 0755);
        std::vector<float> l(1000), r(1000);
        for (size_t i = 0; i < l.size(); ++i) { l[i] = std::sin(static_cast<float>(i) * 0.1f) * 0.5f; r[i] = -l[i]; }
        std::string error;
        for (bool f32 : {false, true}) {
            std::string path = temp_dir() + (f32 ? "/t_f32.wav" : "/t_16.wav");
            REQUIRE(write_wav(path, l, r, 44100.0, f32, error));
            std::vector<float> l2, r2;
            double rate = 0;
            REQUIRE(read_wav(path, l2, r2, rate, error));
            CHECK(rate == 44100.0 && l2.size() == 1000);
            double err = 0;
            for (size_t i = 0; i < l2.size(); ++i) err = std::max(err, static_cast<double>(std::fabs(l2[i] - l[i])) + std::fabs(r2[i] - r[i]));
            CHECK_MSG(err < (f32 ? 1e-9 : 1e-4), "wav error " + std::to_string(err));
        }
        std::vector<float> x, y; double rt;
        CHECK(!read_wav(temp_dir() + "/missing.wav", x, y, rt, error));
    }
    return check::summary("test_engine");
}
