#include "test_main.h"
#include "fakes.h"
#include "core/file_ops.h"
#include <unistd.h>
using namespace dm;

namespace {
struct Fixture {
    TempDir t; Journal journal; FileOps ops; VolumeInfo sd, usb, ro;
    Fixture() : journal(t.sub("journal.jsonl")), ops(journal, 0) {
        t.mkdir("sd/Samples/User"); t.mkdir("sd/autorun"); t.write("sd/autorun/autorun.sh", "#!/bin/sh");
        t.write("sd/Samples/User/kick.wav", std::string(5000, 'k')); t.write("sd/Samples/User/snare.wav", std::string(3000, 's'));
        t.mkdir("sd/Samples/Kit/Sub"); t.write("sd/Samples/Kit/a.wav", "aaa"); t.write("sd/Samples/Kit/Sub/b.wav", "bbbb");
        t.mkdir("usb"); t.mkdir("ro/Samples"); t.write("ro/Samples/f.wav", "f");
        sd.id = "sdcard"; sd.name = "SD"; sd.root = sd.mount_point = t.sub("sd"); sd.mounted = true; sd.policy_writable = true; sd.ownership = Ownership::device;
        sd.kind = VolumeKind::removable; sd.protected_paths = {"autorun", ".diskmode"};
        usb = sd; usb.id = "usb"; usb.name = "USB"; usb.root = usb.mount_point = t.sub("usb"); usb.protected_paths.clear();
        ro = sd; ro.id = "internal"; ro.name = "Internal"; ro.root = ro.mount_point = t.sub("ro"); ro.policy_writable = false; ro.kind = VolumeKind::internal;
    }
    RelPath P(const std::string& s) { RelPath p; parse_rel_path(s, p); return p; }
};
}

TEST(list_sorts_filters_and_hides_data_dir) {
    Fixture f; f.t.mkdir("sd/.diskmode/trash"); f.t.write("sd/.hidden", "");
    Listing l; ListOptions o;
    CHECK_OK(f.ops.list(f.sd, f.P(""), o, l));
    CHECK_EQ(l.entries.size(), 2u);                 // Samples, autorun (hidden entries skipped, .diskmode never shown)
    CHECK_EQ(l.entries[0].name, "autorun"); CHECK_EQ(l.entries[1].name, "Samples"); CHECK(l.entries[0].is_protected); CHECK(!l.entries[1].is_protected);
    o.show_hidden = true; l = Listing{}; CHECK_OK(f.ops.list(f.sd, f.P(""), o, l)); CHECK_EQ(l.entries.size(), 3u);
    o = ListOptions{}; o.sort = "size"; o.descending = true; l = Listing{};
    CHECK_OK(f.ops.list(f.sd, f.P("Samples/User"), o, l)); CHECK_EQ(l.entries[0].name, "kick.wav");
    o = ListOptions{}; o.filter = "SNA"; l = Listing{}; CHECK_OK(f.ops.list(f.sd, f.P("Samples/User"), o, l)); CHECK_EQ(l.entries.size(), 1u);
    o = ListOptions{}; o.limit = 1; o.offset = 1; l = Listing{}; CHECK_OK(f.ops.list(f.sd, f.P("Samples/User"), o, l)); CHECK_EQ(l.entries.size(), 1u); CHECK_EQ(l.total, 2u);
    o = ListOptions{}; o.kind = "dir"; l = Listing{}; CHECK_OK(f.ops.list(f.sd, f.P("Samples"), o, l)); CHECK_EQ(l.entries.size(), 2u);
    CHECK_ERR(f.ops.list(f.sd, f.P("Samples/User/kick.wav"), o, l), Err::invalid_argument);
    CHECK_ERR(f.ops.list(f.sd, f.P("nope"), o, l), Err::not_found);
}

TEST(mkdir_rename_and_conflicts) {
    Fixture f;
    CHECK_OK(f.ops.mkdir(f.sd, f.P("Samples/New"))); CHECK(f.t.exists("sd/Samples/New"));
    CHECK_ERR(f.ops.mkdir(f.sd, f.P("Samples/New")), Err::exists);
    CHECK_ERR(f.ops.mkdir(f.sd, f.P("autorun/x")), Err::forbidden);
    CHECK_ERR(f.ops.mkdir(f.ro, f.P("x")), Err::read_only);
    CHECK_ERR(f.ops.mkdir(f.sd, f.P("missing/x")), Err::not_found);
    CHECK_OK(f.ops.rename(f.sd, f.P("Samples/User/kick.wav"), "Kick 1.wav")); CHECK(f.t.exists("sd/Samples/User/Kick 1.wav"));
    CHECK_ERR(f.ops.rename(f.sd, f.P("Samples/User/snare.wav"), "Kick 1.wav"), Err::exists);   // never silently overwrite
    CHECK_ERR(f.ops.rename(f.sd, f.P("Samples/User/snare.wav"), "../x"), Err::invalid_argument);
    CHECK_ERR(f.ops.rename(f.sd, f.P("Samples"), "autorun"), Err::forbidden);
    CHECK_ERR(f.ops.rename(f.sd, f.P("autorun"), "autorun2"), Err::forbidden);
    CHECK_EQ(f.journal.recent(10).size(), 3u);   // mkdir + rename done + rename failed
}

TEST(copy_file_and_tree_with_conflict_policies) {
    Fixture f; OpContext ctx; std::string name;
    CHECK_OK(f.ops.copy(f.sd, f.P("Samples/User/kick.wav"), f.sd, f.P("Samples/Kit"), "", Conflict::fail, ctx, &name));
    CHECK_EQ(name, "kick.wav"); CHECK_EQ(f.t.read("sd/Samples/Kit/kick.wav").size(), 5000u);
    CHECK_ERR(f.ops.copy(f.sd, f.P("Samples/User/kick.wav"), f.sd, f.P("Samples/Kit"), "", Conflict::fail, ctx, &name), Err::exists);
    CHECK_OK(f.ops.copy(f.sd, f.P("Samples/User/kick.wav"), f.sd, f.P("Samples/Kit"), "", Conflict::rename, ctx, &name));
    CHECK_EQ(name, "kick 2.wav");
    f.t.write("sd/Samples/Kit/kick.wav", "old");
    CHECK_OK(f.ops.copy(f.sd, f.P("Samples/User/kick.wav"), f.sd, f.P("Samples/Kit"), "", Conflict::overwrite, ctx, &name));
    CHECK_EQ(f.t.read("sd/Samples/Kit/kick.wav").size(), 5000u);
    // tree copy, cross volume
    std::uint64_t seen = 0; ctx.progress = [&](std::uint64_t d, std::uint64_t) { seen = d; };
    CHECK_OK(f.ops.copy(f.sd, f.P("Samples/Kit"), f.usb, f.P(""), "KitCopy", Conflict::fail, ctx, &name));
    CHECK(f.t.exists("usb/KitCopy/Sub/b.wav")); CHECK(f.t.exists("usb/KitCopy/kick 2.wav")); CHECK(seen > 0);
    CHECK_ERR(f.ops.copy(f.sd, f.P("Samples"), f.sd, f.P("Samples/Kit"), "", Conflict::fail, ctx), Err::invalid_argument); // into itself
    CHECK_ERR(f.ops.copy(f.sd, f.P("Samples/User/kick.wav"), f.ro, f.P(""), "", Conflict::fail, ctx), Err::read_only);
    CHECK_ERR(f.ops.copy(f.sd, f.P("Samples/User/kick.wav"), f.sd, f.P("autorun"), "", Conflict::fail, ctx), Err::forbidden);
    CHECK_OK(f.ops.copy(f.ro, f.P("Samples/f.wav"), f.sd, f.P("Samples"), "", Conflict::fail, ctx));  // reading from read-only is fine
    // no temp files left behind
    Listing l; ListOptions o; o.show_hidden = true; CHECK_OK(f.ops.list(f.sd, f.P("Samples/Kit"), o, l));
    for (auto& e : l.entries) CHECK(e.name.rfind(".dmtmp-", 0) != 0);
}

TEST(copy_respects_cancel_and_free_space) {
    Fixture f; OpContext ctx; std::atomic<bool> cancel{true}; ctx.cancel = &cancel;
    CHECK_ERR(f.ops.copy(f.sd, f.P("Samples/User/kick.wav"), f.usb, f.P(""), "", Conflict::fail, ctx), Err::cancelled);
    CHECK(!f.t.exists("usb/kick.wav"));
    Listing l; ListOptions o; o.show_hidden = true; CHECK_OK(f.ops.list(f.usb, f.P(""), o, l)); CHECK_EQ(l.entries.size(), 0u);
    // out-of-space: margin larger than any disk
    Journal j2(f.t.sub("j2.jsonl")); FileOps tight(j2, ~0ull >> 1);
    CHECK_ERR(tight.copy(f.sd, f.P("Samples/User/kick.wav"), f.usb, f.P(""), "", Conflict::fail, OpContext{}), Err::no_space);
}

TEST(move_same_volume_and_cross_volume) {
    Fixture f; OpContext ctx; std::string name;
    CHECK_OK(f.ops.move(f.sd, f.P("Samples/User/snare.wav"), f.sd, f.P("Samples/Kit"), "", Conflict::fail, ctx, &name));
    CHECK(!f.t.exists("sd/Samples/User/snare.wav")); CHECK(f.t.exists("sd/Samples/Kit/snare.wav"));
    CHECK_ERR(f.ops.move(f.sd, f.P("Samples/User/kick.wav"), f.sd, f.P("Samples/Kit"), "snare.wav", Conflict::fail, ctx), Err::exists);
    CHECK(f.t.exists("sd/Samples/User/kick.wav"));
    CHECK_OK(f.ops.move(f.sd, f.P("Samples/Kit"), f.usb, f.P(""), "", Conflict::fail, ctx, &name));   // cross-volume tree move = copy + verify + delete
    CHECK(!f.t.exists("sd/Samples/Kit")); CHECK(f.t.exists("usb/Kit/Sub/b.wav")); CHECK(f.t.exists("usb/Kit/snare.wav"));
    CHECK_ERR(f.ops.move(f.sd, f.P("autorun"), f.usb, f.P(""), "", Conflict::fail, ctx), Err::forbidden);
    CHECK_ERR(f.ops.move(f.ro, f.P("Samples/f.wav"), f.sd, f.P(""), "", Conflict::fail, ctx), Err::read_only);
    CHECK_ERR(f.ops.move(f.sd, f.P("Samples"), f.sd, f.P("Samples/User"), "", Conflict::fail, ctx), Err::invalid_argument);
}

TEST(trash_restore_purge_and_permanent_delete) {
    Fixture f; std::string id;
    CHECK_OK(f.ops.remove(f.sd, f.P("Samples/User/kick.wav"), false, &id));
    CHECK(!f.t.exists("sd/Samples/User/kick.wav")); CHECK(f.t.exists("sd/.diskmode/trash/" + id + "/kick.wav"));
    std::vector<TrashItem> items; CHECK_OK(f.ops.list_trash(f.sd, items)); CHECK_EQ(items.size(), 1u); CHECK_EQ(items[0].original_path, "Samples/User/kick.wav"); CHECK_EQ(items[0].size, 5000u);
    std::string restored; CHECK_OK(f.ops.restore(f.sd, id, &restored)); CHECK_EQ(restored, "Samples/User/kick.wav"); CHECK(f.t.exists("sd/Samples/User/kick.wav"));
    items.clear(); CHECK_OK(f.ops.list_trash(f.sd, items)); CHECK_EQ(items.size(), 0u);
    CHECK_ERR(f.ops.restore(f.sd, id, &restored), Err::not_found);
    CHECK_ERR(f.ops.restore(f.sd, "../x", &restored), Err::invalid_argument);
    // restore conflict
    CHECK_OK(f.ops.remove(f.sd, f.P("Samples/User/kick.wav"), false, &id)); f.t.write("sd/Samples/User/kick.wav", "new");
    CHECK_ERR(f.ops.restore(f.sd, id, &restored), Err::exists);
    CHECK_OK(f.ops.purge_trash_item(f.sd, id)); CHECK(!f.t.exists("sd/.diskmode/trash/" + id));
    // folder to trash + permanent
    CHECK_OK(f.ops.remove(f.sd, f.P("Samples/Kit"), false, &id)); CHECK(f.t.exists("sd/.diskmode/trash/" + id + "/Kit/Sub/b.wav"));
    CHECK_OK(f.ops.remove(f.sd, f.P("Samples/User"), true)); CHECK(!f.t.exists("sd/Samples/User"));
    CHECK_ERR(f.ops.remove(f.sd, f.P("autorun"), true), Err::forbidden);
    CHECK_ERR(f.ops.remove(f.sd, f.P(".diskmode"), true), Err::forbidden);
    CHECK_ERR(f.ops.remove(f.sd, f.P(""), true), Err::invalid_argument);
    CHECK_ERR(f.ops.remove(f.ro, f.P("Samples/f.wav"), false), Err::read_only);
    // trash on a volume that cannot host one: list is empty, no error
    items.clear(); CHECK_OK(f.ops.list_trash(f.ro, items)); CHECK_EQ(items.size(), 0u);
}

TEST(hash_open_read_place_and_usage) {
    Fixture f; OpContext ctx; std::string hex;
    CHECK_OK(f.ops.sha256(f.sd, f.P("Samples/Kit/a.wav"), hex, ctx));
    CHECK_EQ(hex, "9834876dcfb05cb167a5c24953eba58c4ac89b1adf57f28f2f9d09af107ee8f0");   // sha256("aaa")
    ScopedFd fd; std::uint64_t size = 0;
    CHECK_OK(f.ops.open_read(f.sd, f.P("Samples/Kit/a.wav"), fd, size)); CHECK_EQ(size, 3u); CHECK(fd.valid());
    CHECK_ERR(f.ops.open_read(f.sd, f.P("Samples/Kit"), fd, size), Err::invalid_argument);
    f.t.write("staged.bin", "staged-content");
    std::string name;
    CHECK_OK(f.ops.place_file(f.t.sub("staged.bin"), f.sd, f.P("Samples/User"), "up.wav", Conflict::fail, ctx, &name));
    CHECK_EQ(name, "up.wav"); CHECK(!f.t.exists("staged.bin")); CHECK_EQ(f.t.read("sd/Samples/User/up.wav"), "staged-content");
    f.t.write("staged2.bin", "x");
    CHECK_ERR(f.ops.place_file(f.t.sub("staged2.bin"), f.sd, f.P("Samples/User"), "up.wav", Conflict::fail, ctx, &name), Err::exists);
    CHECK(f.t.exists("staged2.bin"));   // staged file kept on conflict so the client can retry with another policy
    CHECK_OK(f.ops.place_file(f.t.sub("staged2.bin"), f.sd, f.P("Samples/User"), "up.wav", Conflict::rename, ctx, &name)); CHECK_EQ(name, "up 2.wav");
    CHECK_ERR(f.ops.place_file(f.t.sub("nothing"), f.sd, f.P("Samples/User"), "z.wav", Conflict::fail, ctx), Err::not_found);
    std::uint64_t files, dirs, bytes; CHECK_OK(f.ops.usage(f.sd, f.P("Samples"), files, dirs, bytes));
    CHECK_EQ(files, 6u); CHECK_EQ(dirs, 3u); CHECK_EQ(bytes, 5000u + 3000 + 3 + 4 + 14 + 1);
}

TEST(storage_removal_is_reported_not_crashed) {
    Fixture f; VolumeInfo gone = f.usb; gone.root = f.t.sub("vanished");
    Listing l; ListOptions o;
    CHECK_ERR(f.ops.list(gone, f.P(""), o, l), Err::unavailable);
    gone.mounted = false; CHECK_ERR(f.ops.mkdir(gone, f.P("x")), Err::unavailable);
}
