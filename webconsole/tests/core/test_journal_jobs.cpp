#include "test_main.h"
#include "core/journal.h"
#include "core/job_queue.h"
#include "core/transfer_manager.h"
#include <chrono>
#include <thread>
using namespace dm;

TEST(journal_recovery_marks_interrupted_and_lists_temps) {
    TempDir t; t.write("tmp1", "x");
    {
        Journal j(t.sub("j.jsonl"));
        std::string a = j.begin("copy", "sd:a", "sd:b", {t.sub("tmp1")});
        j.finish(a, "done");
        j.begin("copy", "sd:c", "sd:d", {t.sub("tmp2")});       // never finished (crash)
    }
    Journal j2(t.sub("j.jsonl"));
    auto temps = j2.recover();
    CHECK_EQ(temps.size(), 1u); CHECK_EQ(temps[0], t.sub("tmp2"));
    auto recent = j2.recent(10);
    CHECK_EQ(recent.size(), 2u);
    CHECK_EQ(recent[0].state, "interrupted"); CHECK_EQ(recent[1].state, "done");
    // second recovery finds nothing dangling
    Journal j3(t.sub("j.jsonl")); CHECK_EQ(j3.recover().size(), 0u);
}

TEST(job_queue_runs_cancels_and_bounds) {
    JobQueue q(1, 2);
    std::atomic<int> ran{0};
    std::string a, b, c, d;
    CHECK_OK(q.submit("t", "s1", [&](Job& j) { for (int i = 0; i < 50 && !j.cancel; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5)); ran++; }, a, {JobItem{"sd:x", "", "queued", ""}}));
    for (int i = 0; i < 200 && q.get(a)["state"] != "running"; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK_OK(q.submit("t", "s1", [&](Job&) { ran++; }, b));
    CHECK_OK(q.submit("t", "s2", [&](Job&) { ran++; }, c));
    CHECK_ERR(q.submit("t", "s1", [&](Job&) { ran++; }, d), Err::busy);     // queue full (2 queued + 1 running)
    CHECK(q.any_running_on("sd"));
    CHECK(!q.cancel(b, "s2", false));     // not owner
    CHECK(q.cancel(b, "s1", false));      // queued -> cancelled
    CHECK(q.cancel(a, "other", true));    // admin may cancel
    for (int i = 0; i < 200 && (q.get(c)["state"] != "done"); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK_EQ(q.get(c)["state"], "done"); CHECK_EQ(q.get(b)["state"], "cancelled");
    for (int i = 0; i < 200 && q.get(a)["state"] == "running"; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK_EQ(q.get(a)["state"], "cancelled");
    CHECK_EQ(q.list("s1", false).size(), 2u); CHECK_EQ(q.list("", true).size(), 3u);
    CHECK(q.get("nope").is_null());
}

TEST(job_partial_outcome_from_items) {
    JobQueue q(1, 8); std::string id;
    CHECK_OK(q.submit("batch", "s", [&](Job& j) { q.set_item(j, 0, "done"); q.set_item(j, 1, "failed", "boom"); }, id,
                      {JobItem{"a", "", "queued", ""}, JobItem{"b", "", "queued", ""}}));
    for (int i = 0; i < 200 && q.get(id)["state"] == "queued"; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    for (int i = 0; i < 200 && q.get(id)["state"] == "running"; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    json j = q.get(id); CHECK_EQ(j["state"], "partial"); CHECK_EQ(j["summary"]["failed"], 1); CHECK_EQ(j["items"][1]["error"], "boom");
}

TEST(transfer_manager_chunks_resume_checksum_and_limits) {
    TempDir t; TransferManager tm(t.sub("staging"), 1000, 2);
    std::string id;
    CHECK_ERR(tm.begin("s", "sd", "", "big.wav", 5000, "", false, id), Err::invalid_argument);     // over limit
    CHECK_ERR(tm.begin("s", "sd", "", "../x", 10, "", false, id), Err::invalid_argument);
    CHECK_ERR(tm.begin("s", "sd", "", "x.wav", 10, "nothex", false, id), Err::invalid_argument);
    std::string sha_abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    CHECK_OK(tm.begin("s", "sd", "", "x.wav", 3, sha_abc, false, id));
    CHECK_ERR(tm.append(id, "other", 0, "a", 1), Err::not_found);          // owner mismatch
    CHECK_OK(tm.append(id, "s", 0, "a", 1));
    CHECK_ERR(tm.append(id, "s", 0, "b", 1), Err::invalid_argument);       // offset mismatch -> resume at 1
    CHECK_EQ(tm.get(id, "s")["received"], 1);
    std::string p, h; CHECK_ERR(tm.finish(id, "s", p, h), Err::invalid_argument);   // incomplete
    CHECK_OK(tm.append(id, "s", 1, "bc", 2));
    CHECK_ERR(tm.append(id, "s", 3, "d", 1), Err::invalid_argument);        // more than announced
    CHECK_OK(tm.finish(id, "s", p, h)); CHECK_EQ(h, sha_abc); CHECK_EQ(t.read("staging/" + id + ".part"), "abc");
    // checksum mismatch discards
    std::string id2; CHECK_OK(tm.begin("s", "sd", "", "y.wav", 1, sha_abc, false, id2)); CHECK_OK(tm.append(id2, "s", 0, "z", 1));
    CHECK_ERR(tm.finish(id2, "s", p, h), Err::io); CHECK(!t.exists("staging/" + id2 + ".part"));
    // open-upload limit per owner
    std::string a, b, c;
    CHECK_OK(tm.begin("s", "sd", "", "a", 1, "", false, a)); CHECK_OK(tm.begin("s", "sd", "", "b", 1, "", false, b));
    CHECK_ERR(tm.begin("s", "sd", "", "c", 1, "", false, c), Err::busy);
    CHECK_OK(tm.abort(a, "s")); CHECK(!t.exists("staging/" + a + ".part"));
    t.write("staging/orphan.part", "old");
    CHECK(tm.reap_stale(0) >= 1);
}
