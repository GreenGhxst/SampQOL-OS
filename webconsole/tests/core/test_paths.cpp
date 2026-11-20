#include "test_main.h"
#include "core/path_auth.h"
#include <unistd.h>
using namespace dm;

TEST(parse_rel_path_accepts_normal_paths) {
    RelPath p;
    CHECK_OK(parse_rel_path("Samples/User/kick 01.wav", p));
    CHECK_EQ(p.parts.size(), 3u);
    CHECK_EQ(p.str(), "Samples/User/kick 01.wav");
    CHECK_OK(parse_rel_path("", p)); CHECK(p.empty());
    CHECK_OK(parse_rel_path("a/", p)); CHECK_EQ(p.str(), "a");
    CHECK_OK(parse_rel_path("ümlaut/日本.wav", p));
}

TEST(parse_rel_path_rejects_traversal_and_bad_names) {
    RelPath p;
    CHECK_ERR(parse_rel_path("../etc/passwd", p), Err::invalid_argument);
    CHECK_ERR(parse_rel_path("a/../b", p), Err::invalid_argument);
    CHECK_ERR(parse_rel_path("/abs", p), Err::invalid_argument);
    CHECK_ERR(parse_rel_path("a//b", p), Err::invalid_argument);
    CHECK_ERR(parse_rel_path("a\\b", p), Err::invalid_argument);
    CHECK_ERR(parse_rel_path(".", p), Err::invalid_argument);
    CHECK_ERR(parse_rel_path("a/./b", p), Err::invalid_argument);
    CHECK_ERR(parse_rel_path(std::string("bad\x01name"), p), Err::invalid_argument);
    CHECK_ERR(parse_rel_path("what?.wav", p), Err::invalid_argument);
    CHECK_ERR(parse_rel_path(std::string(300, 'a'), p), Err::invalid_argument);
    CHECK_ERR(validate_name("trailing."), Err::invalid_argument);
    CHECK_ERR(validate_name("con:fig"), Err::invalid_argument);
    CHECK_OK(validate_name("Kick (1) - final.wav"));
}

static VolumeInfo vol(const std::string& root) {
    VolumeInfo v; v.id = "t"; v.name = "T"; v.root = root; v.mount_point = root; v.mounted = true; v.policy_writable = true;
    v.ownership = Ownership::device; v.protected_paths = {"autorun", "mpcqol.conf"}; v.kind = VolumeKind::removable; return v;
}

TEST(resolve_refuses_symlinks_anywhere) {
    TempDir t; t.mkdir("real/inner"); t.write("real/inner/f.txt", "x"); t.mkdir("outside"); t.write("outside/secret", "s");
    CHECK_EQ(::symlink(t.sub("outside").c_str(), t.sub("real/link").c_str()), 0);
    CHECK_EQ(::symlink(t.sub("outside/secret").c_str(), t.sub("real/flink").c_str()), 0);
    VolumeInfo v = vol(t.sub("real"));
    Resolved r;
    CHECK_OK(PathAuthorizer::resolve(v, "inner/f.txt", r, true)); CHECK(r.is_file());
    CHECK_ERR(PathAuthorizer::resolve(v, "link/secret", r, true), Err::forbidden);   // intermediate symlink
    CHECK_ERR(PathAuthorizer::resolve(v, "flink", r, true), Err::forbidden);         // leaf symlink
    CHECK_ERR(PathAuthorizer::resolve(v, "link", r, false), Err::forbidden);
    CHECK_ERR(PathAuthorizer::resolve(v, "missing/x", r, false), Err::not_found);
    CHECK_OK(PathAuthorizer::resolve(v, "inner/new.txt", r, false)); CHECK(!r.exists); CHECK_EQ(r.leaf, "new.txt");
    CHECK_OK(PathAuthorizer::resolve(v, "", r, true)); CHECK(r.is_dir()); CHECK(r.leaf.empty());
}

TEST(write_checks_enforce_policy_and_protected_paths) {
    TempDir t; t.mkdir("root/autorun"); t.write("root/mpcqol.conf", "");
    VolumeInfo v = vol(t.sub("root"));
    RelPath p;
    parse_rel_path("autorun/autorun.sh", p); CHECK_ERR(PathAuthorizer::check_write(v, p), Err::forbidden);
    parse_rel_path("mpcqol.conf", p); CHECK_ERR(PathAuthorizer::check_write(v, p), Err::forbidden);
    parse_rel_path("autorun-off/x", p); CHECK_OK(PathAuthorizer::check_write(v, p));   // prefix must be a whole component
    parse_rel_path("", p); CHECK_ERR(PathAuthorizer::check_write(v, p), Err::forbidden);
    parse_rel_path("Samples/a.wav", p); CHECK_OK(PathAuthorizer::check_write(v, p));
    v.policy_writable = false; CHECK_ERR(PathAuthorizer::check_write(v, p), Err::read_only);
    v.policy_writable = true; v.fs_read_only = true; CHECK_ERR(PathAuthorizer::check_write(v, p), Err::read_only);
    v.fs_read_only = false; v.ownership = Ownership::usb_host; CHECK_ERR(PathAuthorizer::check_write(v, p), Err::busy); CHECK_ERR(PathAuthorizer::check_read(v), Err::busy);
    v.ownership = Ownership::device; v.mounted = false; CHECK_ERR(PathAuthorizer::check_read(v), Err::unavailable);
}
