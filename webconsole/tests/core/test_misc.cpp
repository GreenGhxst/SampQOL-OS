#include "test_main.h"
#include "fakes.h"
#include "core/inbox.h"
#include "core/intelligence.h"
#include "core/screen.h"
#include "core/wav.h"
#include "util/sha256.h"
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
using namespace dm;

static std::string make_wav(int channels, int bits, int frames, int format = 1) {
    std::string d; auto le32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) d.push_back(char(v >> (8 * i))); };
    auto le16 = [&](std::uint16_t v) { d.push_back(char(v)); d.push_back(char(v >> 8)); };
    int block = channels * bits / 8; std::uint32_t data = frames * block;
    d += "RIFF"; le32(36 + data); d += "WAVE"; d += "fmt "; le32(16); le16(format); le16(channels); le32(44100); le32(44100 * block); le16(block); le16(bits);
    d += "data"; le32(data);
    for (int f = 0; f < frames; ++f) for (int c = 0; c < channels; ++c) {
        double v = (f % 100) < 50 ? 0.5 : -0.5;
        if (format == 3) { float fv = float(v); char b[4]; std::memcpy(b, &fv, 4); d.append(b, 4); }
        else if (bits == 16) le16(std::uint16_t(std::int16_t(v * 32767)));
        else if (bits == 24) { std::int32_t s = std::int32_t(v * 8388607); d.push_back(char(s)); d.push_back(char(s >> 8)); d.push_back(char(s >> 16)); }
        else if (bits == 8) d.push_back(char(int(v * 127) + 128));
    }
    return d;
}

TEST(wav_probe_and_peaks) {
    TempDir t; t.write("a.wav", make_wav(2, 16, 44100)); t.write("b.wav", make_wav(1, 24, 1000)); t.write("f.wav", make_wav(1, 32, 1000, 3)); t.write("bad.wav", "not a wav");
    int fd = ::open(t.sub("a.wav").c_str(), O_RDONLY);
    WavInfo w = wav_probe(fd); CHECK(w.valid && w.supported); CHECK_EQ(w.channels, 2); CHECK_EQ(w.bits, 16); CHECK_EQ(w.frames, 44100u); CHECK(w.seconds > 0.99 && w.seconds < 1.01);
    std::vector<float> peaks; CHECK_OK(wav_peaks(fd, w, 50, peaks)); CHECK_EQ(peaks.size(), 100u); CHECK(peaks[1] > 0.4f && peaks[0] < -0.4f); ::close(fd);
    fd = ::open(t.sub("b.wav").c_str(), O_RDONLY); w = wav_probe(fd); CHECK(w.supported); CHECK_EQ(w.bits, 24); CHECK_OK(wav_peaks(fd, w, 10, peaks)); CHECK(peaks[1] > 0.4f); ::close(fd);
    fd = ::open(t.sub("f.wav").c_str(), O_RDONLY); w = wav_probe(fd); CHECK(w.supported); CHECK_EQ(w.format, 3); CHECK_OK(wav_peaks(fd, w, 10, peaks)); CHECK(peaks[1] > 0.4f); ::close(fd);
    fd = ::open(t.sub("bad.wav").c_str(), O_RDONLY); w = wav_probe(fd); CHECK(!w.valid); CHECK_ERR(wav_peaks(fd, w, 10, peaks), Err::unsupported); ::close(fd);
}

namespace {
struct World {
    TempDir t; Journal journal; FileOps ops; std::unique_ptr<VolumeRegistry> reg; std::unique_ptr<Intelligence> intel; std::unique_ptr<Inbox> inbox;
    World() : journal(t.sub("data/journal.jsonl")), ops(journal, 0) {
        t.mkdir("data"); t.mkdir("sd/Samples/User"); t.mkdir("sd/autorun");
        t.write("sd/Samples/User/kick.wav", make_wav(1, 16, 500)); t.write("sd/Samples/User/kick copy.wav", make_wav(1, 16, 500));
        t.write("sd/Samples/User/hat.wav", make_wav(1, 16, 300)); t.write("sd/Samples/song.xpj", "proj"); t.write("sd/autorun/secret.txt", "s");
        auto p = std::make_unique<FakeProbe>(); p->table = {{"/dev/mmcblk1p1", t.sub("sd"), "exfat", "rw"}};
        auto rules = test_rules(); rules[0].mount_prefix = t.sub("s");
        reg = std::make_unique<VolumeRegistry>(rules, std::move(p));
        intel = std::make_unique<Intelligence>(t.sub("data"), *reg, ops); intel->load();
        inbox = std::make_unique<Inbox>(t.sub("data"), ops, *intel, *reg); inbox->load();
    }
};
}

TEST(intelligence_index_search_duplicates_collections) {
    World w; std::size_t n = 0;
    CHECK_OK(w.intel->scan("sdcard", nullptr, n, 0)); CHECK_EQ(n, 4u);     // autorun (protected) is not indexed
    SearchQuery q; q.text = "kick"; json r = w.intel->search(q); CHECK_EQ(r["total"], 2);
    q = SearchQuery{}; q.kind = "project"; r = w.intel->search(q); CHECK_EQ(r["total"], 1); CHECK_EQ(r["results"][0]["name"], "song.xpj");
    q = SearchQuery{}; q.text = "secret"; CHECK_EQ(w.intel->search(q)["total"], 0);
    json d = w.intel->duplicates("sdcard", nullptr); CHECK_EQ(d["groups"].size(), 1u); CHECK_EQ(d["groups"][0]["files"].size(), 2u);
    json counts = w.intel->dashboard_counts("sdcard"); CHECK_EQ(counts["files"], 4); CHECK_EQ(counts["by_kind"]["audio"]["files"], 3);
    std::string cid; CHECK_OK(w.intel->collection_create("Drums", cid));
    CHECK_OK(w.intel->collection_add(cid, "sdcard", "Samples/User/kick.wav")); CHECK_OK(w.intel->collection_add(cid, "sdcard", "Samples/User/kick.wav"));
    CHECK_ERR(w.intel->collection_add(cid, "sdcard", "../x"), Err::invalid_argument); CHECK_ERR(w.intel->collection_add("zzz", "sdcard", "a"), Err::not_found);
    CHECK_EQ(w.intel->collections()[0]["items"].size(), 1u); CHECK(w.t.exists("sd/Samples/User/kick.wav"));   // never moved
    CHECK_OK(w.intel->favorite("sdcard", "Samples/User/hat.wav", true)); CHECK_EQ(w.intel->favorites().size(), 1u);
    w.intel->touch_recent("sdcard", "Samples/User/hat.wav"); w.intel->touch_recent("sdcard", "Samples/User/hat.wav"); CHECK_EQ(w.intel->recents().size(), 1u);
    CHECK_OK(w.intel->set_tags("sdcard", "Samples/User/hat.wav", {"perc", "closed"})); CHECK_EQ(w.intel->tags("sdcard", "Samples/User/hat.wav").size(), 2u);
    // persistence
    Intelligence again(w.t.sub("data"), *w.reg, w.ops); again.load();
    CHECK_EQ(again.collections().size(), 1u); CHECK_EQ(again.search(SearchQuery{})["total"], 4);
    // batch rename
    auto v = *w.reg->get("sdcard"); RelPath dir; parse_rel_path("Samples/User", dir);
    auto plan = w.intel->rename_plan(v, dir, {"kick.wav", "hat.wav", "kick copy.wav"}, "Drum {n2}{ext}", 1);
    CHECK_EQ(plan[0].to, "Drum 01.wav"); CHECK_EQ(plan[2].to, "Drum 03.wav"); CHECK(plan[0].problem.empty());
    auto bad = w.intel->rename_plan(v, dir, {"kick.wav", "hat.wav"}, "same.wav", 1); CHECK_EQ(bad[1].problem, "duplicate target name");
    json res = w.intel->rename_apply(v, dir, plan); CHECK_EQ(res[0]["state"], "done"); CHECK(w.t.exists("sd/Samples/User/Drum 01.wav"));
}

TEST(inbox_stage_probe_duplicates_commit) {
    World w; std::size_t n; w.intel->scan("sdcard", nullptr, n, 0);
    w.t.write("data/staging.bin", make_wav(1, 16, 500));   // identical to kick.wav
    std::string id; CHECK_OK(w.inbox->add(w.t.sub("data/staging.bin"), "upload.wav", Sha256::hex_of(make_wav(1, 16, 500)), id));
    json it = w.inbox->get(id); CHECK(it["wav"]["supported"] == true); CHECK_EQ(it["duplicates"].size(), 2u);
    std::vector<float> peaks; CHECK_OK(w.inbox->peaks(id, 20, peaks)); CHECK_EQ(peaks.size(), 40u);
    CHECK_ERR(w.inbox->rename(id, "bad/name"), Err::invalid_argument); CHECK_OK(w.inbox->rename(id, "kick.wav"));
    auto v = *w.reg->get("sdcard"); RelPath dir; parse_rel_path("Samples/User", dir); std::string fp;
    CHECK_ERR(w.inbox->commit(id, v, dir, Conflict::fail, fp), Err::exists);          // never overwrite the original by default
    CHECK_EQ(w.inbox->count(), 1u);
    RelPath pdir; parse_rel_path("autorun", pdir); CHECK_ERR(w.inbox->commit(id, v, pdir, Conflict::fail, fp), Err::forbidden);
    CHECK_OK(w.inbox->commit(id, v, dir, Conflict::rename, fp)); CHECK_EQ(fp, "Samples/User/kick 2.wav"); CHECK_EQ(w.inbox->count(), 0u);
    w.t.write("data/s2.bin", "zzz"); CHECK_OK(w.inbox->add(w.t.sub("data/s2.bin"), "notes.txt", "x", id)); CHECK(w.inbox->get(id)["note"].get<std::string>().find("not an audio") == 0);
    CHECK_OK(w.inbox->discard(id)); CHECK_ERR(w.inbox->discard(id), Err::not_found);
}

TEST(screen_sources_are_honest_about_simulation) {
    auto sim = make_simulated_frame_source(); Frame f;
    CHECK(sim->available()); CHECK(sim->simulated()); CHECK_OK(sim->capture(f)); CHECK_OK(validate_frame(f)); CHECK(f.simulated); CHECK_EQ(f.source, "simulated");
    TempDir t; auto file = make_file_frame_source(t.sub("frame.rgb565"));
    CHECK(!file->available()); CHECK_ERR(file->capture(f), Err::unavailable);
    t.write("frame.rgb565", std::string(320 * 240 * 2, '\x1f')); CHECK(file->available()); CHECK_OK(file->capture(f)); CHECK(!f.simulated); CHECK_EQ(f.source, "file");
    t.write("frame.rgb565", "short"); CHECK_ERR(file->capture(f), Err::invalid_argument);
    std::string ppm = "P6\n320 240\n255\n" + std::string(320 * 240 * 3, '\x80'); t.write("frame.ppm", ppm);
    auto ppmsrc = make_file_frame_source(t.sub("frame.ppm")); CHECK_OK(ppmsrc->capture(f)); CHECK_OK(validate_frame(f));
    auto native = make_unavailable_native_source("no readback"); CHECK(!native->available()); CHECK_EQ(native->unavailable_reason(), "no readback");
    Frame bad; bad.width = 100; CHECK_ERR(validate_frame(bad), Err::invalid_argument);
}
