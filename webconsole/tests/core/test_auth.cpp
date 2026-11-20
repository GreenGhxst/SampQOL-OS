#include "test_main.h"
#include "server/auth.h"
#include "util/sha256.h"
#include <unistd.h>
using namespace dm;

TEST(sha256_and_hmac_vectors) {
    CHECK_EQ(Sha256::hex_of(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_EQ(Sha256::hex_of("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_EQ(Sha256::hex_of(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // RFC 4231 test case 2
    CHECK_EQ(hmac_sha256_hex("Jefe", "what do ya want for nothing?"), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    // RFC 6070-style PBKDF2-HMAC-SHA256 vector (password, salt, 1 iteration): 120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b
    std::string salt_hex = to_hex(reinterpret_cast<const std::uint8_t*>("salt"), 4);
    CHECK_EQ(pbkdf2_sha256_hash("password", salt_hex, 1), "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK_EQ(pbkdf2_sha256_hash("password", salt_hex, 4096), "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    std::string stored = make_password_hash("hunter22");
    CHECK(verify_password_hash("hunter22", stored)); CHECK(!verify_password_hash("hunter2", stored)); CHECK(!verify_password_hash("x", "garbage"));
    CHECK(constant_time_equal("abc", "abc")); CHECK(!constant_time_equal("abc", "abd")); CHECK(!constant_time_equal("abc", "abcd"));
}

static AuthConfig cfg() {
    AuthConfig c; c.pairing_code = "SECRET12"; c.max_failures = 3; c.lockout_ms = 100000;
    std::string salt = random_token(16);
    c.admin_password_hash = "pbkdf2-sha256$2000$" + salt + "$" + pbkdf2_sha256_hash("adminpw", salt, 2000);   // cheap for tests
    return c;
}

TEST(pairing_roles_and_lockout) {
    AuthService a(cfg()); Session s;
    CHECK_ERR(a.pair("wrong", Role::file_manager, "1.2.3.4", "ua", true, s), Err::forbidden);
    CHECK_ERR(a.pair("SECRET12", Role::admin, "1.2.3.4", "ua", true, s), Err::invalid_argument);   // admin cannot be requested by pairing
    CHECK_OK(a.pair("SECRET12", Role::file_manager, "1.2.3.4", "ua", true, s));
    CHECK_EQ(int(s.role), int(Role::file_manager)); CHECK_EQ(s.id.size(), 64u); CHECK_EQ(s.csrf.size(), 64u);
    Session untrusted; CHECK_OK(a.pair("SECRET12", Role::file_manager, "5.6.7.8", "ua", false, untrusted));
    CHECK_EQ(int(untrusted.role), int(Role::viewer));           // plain HTTP never grants write access
    CHECK_ERR(a.elevate(untrusted.id, "adminpw", "5.6.7.8"), Err::forbidden);
    for (int i = 0; i < 3; ++i) a.pair("nope", Role::viewer, "9.9.9.9", "ua", true, s);
    CHECK_ERR(a.pair("SECRET12", Role::viewer, "9.9.9.9", "ua", true, s), Err::busy);   // locked out even with the right code
    CHECK_OK(a.pair("SECRET12", Role::viewer, "9.9.9.10", "ua", true, s));            // other client unaffected
}

TEST(sessions_authenticate_expire_revoke_csrf) {
    AuthConfig c = cfg(); c.idle_timeout_ms = 300; AuthService a(c); Session s;
    CHECK_OK(a.pair("SECRET12", Role::file_manager, "1.1.1.1", "ua", true, s));
    Session live; CHECK_OK(a.authenticate(s.id, live)); CHECK_EQ(live.csrf, s.csrf);
    CHECK_ERR(a.authenticate("", live), Err::forbidden); CHECK_ERR(a.authenticate(std::string(64, 'a'), live), Err::forbidden);
    CHECK(a.check_csrf(s.id, s.csrf)); CHECK(!a.check_csrf(s.id, "")); CHECK(!a.check_csrf(s.id, s.csrf + "x"));
    CHECK_ERR(a.elevate(s.id, "bad", "1.1.1.1"), Err::forbidden);
    CHECK_OK(a.elevate(s.id, "adminpw", "1.1.1.1")); CHECK_OK(a.authenticate(s.id, live)); CHECK_EQ(int(live.role), int(Role::admin));
    json list = a.list_sessions(s.id); CHECK_EQ(list.size(), 1u); CHECK(list[0]["current"] == true); CHECK(!list[0].contains("id"));
    std::string handle = list[0]["handle"];
    CHECK(!a.revoke_by_handle("nope")); CHECK(a.revoke_by_handle(handle));
    CHECK_ERR(a.authenticate(s.id, live), Err::forbidden);
    Session s2; CHECK_OK(a.pair("SECRET12", Role::viewer, "1.1.1.1", "ua", true, s2));
    ::usleep(400000);
    CHECK_ERR(a.authenticate(s2.id, live), Err::forbidden);   // idle expiry
    AuthConfig c2 = cfg(); c2.admin_password_hash.clear(); AuthService b(c2); Session s3;
    CHECK_OK(b.pair("SECRET12", Role::viewer, "1.1.1.1", "ua", true, s3));
    CHECK_ERR(b.elevate(s3.id, "anything", "1.1.1.1"), Err::unsupported);   // admin disabled when no hash configured
}
