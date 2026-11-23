#include "test_main.h"
#include "native/kms_backend.h"
#include <stdexcept>
using namespace dm::native;
namespace {
struct Guard final : RecoveryGuard {
    bool allow=false;
    int approvals=0;
    int revoke_at=0;
    bool verified(std::string& reason) override {
        ++approvals;
        if(!allow || (revoke_at && approvals>=revoke_at)){reason="Recovery supervisor not ready";return false;}
        return true;
    }
};
struct FakeKms final : KmsDriver {
    int fail=0,stage=0,opens=0,saves=0,masters=0,allocs=0,activates=0,blits=0,restores=0;
    bool throw_on_open=false,throw_on_blit=false;
    bool step(int n,std::string& why) {
        stage=n;
        if(fail==n){why="Injected DRM failure";return false;}
        return true;
    }
    bool open_and_find_panel(std::string& why) override {
        ++opens;
        if(throw_on_open)throw std::runtime_error("DRM open exploded");
        return step(1,why);
    }
    bool save_previous_state(std::string& why) override {++saves;return step(2,why);}
    bool acquire_master(std::string& why) override {++masters;return step(3,why);}
    bool allocate_xrgb8888(std::string& why) override {++allocs;return step(4,why);}
    bool activate(std::string& why) override {++activates;return step(5,why);}
    bool blit(const std::vector<std::uint8_t>&,std::string& why) override {
        ++blits;
        if(throw_on_blit)throw std::runtime_error("DRM write exploded");
        return step(6,why);
    }
    void restore_and_close() noexcept override {++restores;}
};
}
TEST(kms_denied_without_external_recovery_no_drm_call) {
    NoRecoveryGuard recovery;FakeKms driver;KmsDisplayBackend backend(recovery,driver);
    std::string why;
    CHECK(!backend.acquire(why));CHECK(!backend.acquired());CHECK(!why.empty());
    CHECK_EQ(driver.opens,0);
    CHECK_EQ(driver.restores,0);
}
TEST(kms_failure_injection_restores_all_stages) {
    for(int fail=1;fail<=5;++fail){
        Guard g;g.allow=true;FakeKms d;d.fail=fail;std::string why;
        KmsDisplayBackend b(g,d);
        CHECK(!b.acquire(why));CHECK(!b.acquired());CHECK_EQ(d.restores,1);
        CHECK(!why.empty());
        b.release();CHECK_EQ(d.restores,2);
    }
}
TEST(kms_revocation_before_claim_and_modeset) {
    for(int at : {2,3}){
        Guard g;g.allow=true;g.revoke_at=at;
        FakeKms d;KmsDisplayBackend b(g,d);std::string why;
        CHECK(!b.acquire(why));CHECK_EQ(d.restores,1);
        if(at==2)CHECK_EQ(d.masters,0);
        if(at==3)CHECK_EQ(d.activates,0);
    }
}
TEST(kms_success_and_submit_failure_restore) {
    Guard g;g.allow=true;FakeKms d;KmsDisplayBackend b(g,d);std::string why;
    CHECK(b.acquire(why));CHECK(b.acquired());
    CHECK(b.acquire(why));CHECK_EQ(d.opens,1);
    CHECK(!b.submit({},why));CHECK(b.acquired());CHECK_EQ(d.blits,0);
    CHECK(b.submit(std::vector<std::uint8_t>(320*240*4),why));CHECK_EQ(d.blits,1);
    d.fail=6;
    CHECK(!b.submit(std::vector<std::uint8_t>(320*240*4),why));
    CHECK(!b.acquired());CHECK_EQ(d.restores,1);
}
TEST(kms_recovers_from_driver_exception) {
    Guard g;g.allow=true;FakeKms d;d.throw_on_open=true;
    KmsDisplayBackend b(g,d);std::string why;
    CHECK(!b.acquire(why));CHECK_EQ(d.restores,1);
    d.throw_on_open=false;CHECK(b.acquire(why));
    d.throw_on_blit=true;
    CHECK(!b.submit(std::vector<std::uint8_t>(320*240*4),why));
    CHECK(!b.acquired());CHECK_EQ(d.restores,2);
}
TEST(kms_guard_revoke_during_frame_restores) {
    Guard g;g.allow=true;FakeKms d;std::string why;
    KmsDisplayBackend b(g,d);CHECK(b.acquire(why));
    g.allow=false;
    CHECK(!b.submit(std::vector<std::uint8_t>(320*240*4),why));
    CHECK(!b.acquired());CHECK_EQ(d.restores,1);CHECK_EQ(d.blits,0);
}
TEST(kms_default_build_remains_off) {
#ifndef DM_WITH_DRM
    CHECK(!make_libdrm_driver());
#else
    // Constructing an implementation is not permission to open a DRM node.
    auto driver=make_libdrm_driver();
    CHECK(bool(driver));
#endif
}

TEST(kms_repeated_start_rechecks_supervisor_authorization) {
    Guard g;g.allow=true;FakeKms d;KmsDisplayBackend b(g,d);std::string why;
    CHECK(b.acquire(why));CHECK_EQ(d.opens,1);
    g.allow=false;
    CHECK(!b.acquire(why));
    CHECK(!b.acquired());CHECK_EQ(d.restores,1);
    CHECK_EQ(d.opens,1);
}
