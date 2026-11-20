#include "test_main.h"
#include "fakes.h"
#include "core/format_guard.h"
#include <unistd.h>
using namespace dm;

namespace {
struct FG {
    TempDir t; std::unique_ptr<VolumeRegistry> reg; JobQueue jobs{1, 8}; FakeProbe* probe = nullptr;
    std::vector<BlockDevice> devs;
    FG() {
        t.mkdir("sd"); t.mkdir("data/UserContent/Samples"); t.mkdir("usb");
        auto p = std::make_unique<FakeProbe>(); probe = p.get();
        p->table = {{"/dev/mmcblk1p1", t.sub("sd"), "exfat", "rw"}, {"/dev/mmcblk0p9", t.sub("data"), "ext4", "rw"}, {"/dev/sda1", t.sub("usb"), "vfat", "rw"}};
        auto rules = test_rules(); rules[0].mount_prefix = t.sub("s"); rules[1].mount_point = t.sub("data"); rules[2].mount_prefix = t.sub("usb");
        reg = std::make_unique<VolumeRegistry>(rules, std::move(p));
        devs = {{"mmcblk1", true, "flash_sd", 32ull << 30, {"mmcblk1p1"}, false},
                {"mmcblk0", false, "", 8ull << 30, {"mmcblk0p9"}, true},
                {"sda", true, "", 64ull << 30, {"sda1"}, false}};
    }
    FormatGuard guard() { return FormatGuard(*reg, jobs, make_fake_block_inventory(devs), make_simulated_format_executor()); }
    static bool check(const json& r, const std::string& name) { for (auto& c : r["checks"]) if (c["name"] == name) return c["pass"]; throw std::runtime_error("no check " + name); }
};
}

TEST(format_preflight_passes_only_for_verified_removable_card) {
    FG f; auto g = f.guard();
    json r = g.preflight("sdcard", "sess");
    CHECK(r["eligible"] == true); CHECK(r.contains("token")); CHECK(r["simulation"] == true); CHECK_EQ(r["device"], "/dev/mmcblk1");
    json c = g.confirm(r["token"], "sdcard", "/dev/mmcblk1", "exfat", "SAMPLES", "sess");
    CHECK(c["ok"] == true); CHECK(c["simulation"] == true); CHECK(c["result"]["simulated"] == true);
    CHECK(c["result"]["would_run"].get<std::string>().find("--volume sdcard") != std::string::npos);
    CHECK(c["result"]["would_run"].get<std::string>().find("mkfs") == std::string::npos);
    json again = g.confirm(r["token"], "sdcard", "/dev/mmcblk1", "exfat", "", "sess");   // single use
    CHECK(again["ok"] == false);
}

TEST(format_refuses_internal_system_unknown_usb_busy_and_app_medium) {
    FG f;
    { auto g = f.guard(); json r = g.preflight("internal", "s"); CHECK(r["eligible"] == false); CHECK(!r.contains("token"));
      CHECK(!FG::check(r, "not_internal_storage")); CHECK(!FG::check(r, "policy_format_candidate")); CHECK(!FG::check(r, "device_not_internal_emmc")); CHECK(!FG::check(r, "device_not_system")); }
    { auto g = f.guard(); json r = g.preflight("usb", "s"); CHECK(r["eligible"] == false); CHECK(!FG::check(r, "device_is_removable_sd")); CHECK(!FG::check(r, "policy_format_candidate")); }
    { auto g = f.guard(); json r = g.preflight("nope", "s"); CHECK(r["eligible"] == false); CHECK(!FG::check(r, "volume_known")); }
    // unknown device: card mounted from a device not in the inventory
    { f.probe->table[0].device = "/dev/mmcblk2p1"; auto g = f.guard(); json r = g.preflight("sdcard", "s"); CHECK(r["eligible"] == false); CHECK(!FG::check(r, "block_device_discovered")); f.probe->table[0].device = "/dev/mmcblk1p1"; }
    // exported to USB host
    { f.probe->exported = {"mmcblk1"}; auto g = f.guard(); json r = g.preflight("sdcard", "s"); CHECK(r["eligible"] == false); CHECK(!FG::check(r, "not_exported_to_usb_host")); f.probe->exported.clear(); }
    // application medium
    { f.probe->app_medium = f.t.sub("sd"); auto g = f.guard(); json r = g.preflight("sdcard", "s"); CHECK(r["eligible"] == false); CHECK(!FG::check(r, "not_application_medium")); f.probe->app_medium.clear(); }
    // busy: a job references the volume
    { std::string id; f.jobs.submit("copy", "s", [](Job& j) { while (!j.cancel) ::usleep(2000); }, id, {JobItem{"sdcard:x", "", "queued", ""}});
      ::usleep(20000); auto g = f.guard(); json r = g.preflight("sdcard", "s"); CHECK(r["eligible"] == false); CHECK(!FG::check(r, "no_active_jobs")); f.jobs.cancel(id, "s", true); f.jobs.shutdown(); }
}

TEST(format_confirm_rejects_mismatched_tokens_and_inputs) {
    FG f; auto g = f.guard();
    json r = g.preflight("sdcard", "sess-A");
    CHECK(g.confirm("bogus", "sdcard", "/dev/mmcblk1", "exfat", "", "sess-A")["ok"] == false);
    CHECK(g.confirm(r["token"], "sdcard", "/dev/mmcblk1", "exfat", "", "sess-B")["ok"] == false);   // other session; token consumed
    r = g.preflight("sdcard", "sess-A");
    CHECK(g.confirm(r["token"], "sdcard", "/dev/mmcblk0", "exfat", "", "sess-A")["ok"] == false);   // device swap
    r = g.preflight("sdcard", "sess-A");
    CHECK(g.confirm(r["token"], "usb", "/dev/mmcblk1", "exfat", "", "sess-A")["ok"] == false);      // volume swap
    r = g.preflight("sdcard", "sess-A");
    CHECK(g.confirm(r["token"], "sdcard", "/dev/mmcblk1", "ext4", "", "sess-A")["ok"] == false);    // unsupported fs
    r = g.preflight("sdcard", "sess-A");
    CHECK(g.confirm(r["token"], "sdcard", "/dev/mmcblk1", "exfat", "bad;label", "sess-A")["ok"] == false);
    r = g.preflight("sdcard", "sess-A");
    CHECK(g.confirm(r["token"], "sdcard", "/dev/mmcblk1", "exfat", "TOO LONG LABEL", "sess-A")["ok"] == false);
    // state changed between preflight and confirm
    r = g.preflight("sdcard", "sess-A"); f.probe->exported = {"mmcblk1"};
    json c = g.confirm(r["token"], "sdcard", "/dev/mmcblk1", "exfat", "", "sess-A"); CHECK(c["ok"] == false); CHECK(c.contains("checks"));
}
