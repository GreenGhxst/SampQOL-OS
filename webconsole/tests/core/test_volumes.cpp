#include "test_main.h"
#include "fakes.h"
using namespace dm;

TEST(registry_maps_mounts_to_policy_volumes) {
    TempDir t; t.mkdir("sd"); t.mkdir("data/UserContent/Samples"); t.mkdir("usb1");
    auto probe = std::make_unique<FakeProbe>();
    probe->table = {{"/dev/mmcblk1p1", t.sub("sd"), "fake", "rw,relatime"}, {"/dev/mmcblk0p9", t.sub("data"), "ext4", "rw"}, {"/dev/sda1", t.sub("usb1"), "vfat", "ro"}};
    // rules refer to /media/ and /data: rewrite to temp paths
    auto rules = test_rules();
    rules[0].mount_prefix = t.sub("s"); rules[1].mount_point = t.sub("data"); rules[2].mount_prefix = t.sub("usb");
    probe->app_medium = t.sub("sd");
    VolumeRegistry reg(rules, std::move(probe));
    auto vols = reg.list(); CHECK_EQ(vols.size(), 3u);
    auto sd = reg.get("sdcard"); CHECK(sd && sd->mounted); CHECK(sd->writable()); CHECK(sd->is_application_medium); CHECK_EQ(sd->device, "/dev/mmcblk1p1");
    CHECK_EQ(sd->fs_type, "fake");
    auto in = reg.get("internal"); CHECK(in && in->mounted); CHECK(!in->writable()); CHECK(!in->policy_writable); CHECK_EQ(in->root, t.sub("data") + "/UserContent/Samples");
    auto usb = reg.get("usb"); CHECK(usb && usb->mounted); CHECK(usb->fs_read_only); CHECK(!usb->writable());
    CHECK(!reg.get("nope"));
    CHECK(!VolumeRegistry::valid_id("../x")); CHECK(!VolumeRegistry::valid_id("")); CHECK(VolumeRegistry::valid_id("sd-card_1"));
    json j = sd->to_json(); CHECK(j["allowed_ops"].size() > 5); CHECK_EQ(j["ownership"], "device");
}

TEST(registry_detects_usb_host_ownership_and_unmount) {
    TempDir t; t.mkdir("sd");
    auto probe = std::make_unique<FakeProbe>(); FakeProbe* p = probe.get();
    probe->table = {{"/dev/mmcblk1p1", t.sub("sd"), "exfat", "rw"}};
    auto rules = test_rules(); rules[0].mount_prefix = t.sub("s");
    VolumeRegistry reg(rules, std::move(probe));
    CHECK(reg.get("sdcard")->writable());
    p->exported = {"mmcblk1"}; reg.refresh();
    auto sd = reg.get("sdcard"); CHECK_EQ(int(sd->ownership), int(Ownership::usb_host)); CHECK(!sd->writable()); CHECK(sd->allowed_ops().empty());
    p->exported.clear(); p->table.clear(); reg.refresh();
    sd = reg.get("sdcard"); CHECK(!sd->mounted); CHECK_EQ(int(sd->ownership), int(Ownership::unmounted));
    auto in = reg.get("internal"); CHECK(!in->mounted);
}

TEST(registry_default_rules_are_read_only_internally) {
    for (const auto& r : VolumeRegistry::default_rules()) {
        if (r.kind == VolumeKind::internal) { CHECK(!r.writable); CHECK(!r.format_candidate); }
        if (r.id == "sdcard") { CHECK(r.writable); CHECK(std::find(r.protected_paths.begin(), r.protected_paths.end(), "autorun") != r.protected_paths.end()); }
    }
    json j = json::parse(R"([{"id":"x","mount_point":"/x","writable":true,"kind":"usb"}])");
    auto rules = VolumeRegistry::rules_from_json(j); CHECK_EQ(rules.size(), 1u); CHECK_EQ(int(rules[0].kind), int(VolumeKind::usb));
}
