#include "test_main.h"
#include "fakes.h"
#include "native/startup_preflight.h"
#include <stdexcept>
using namespace dm;
using namespace dm::native;
namespace {
struct TestAuthority:OwnershipAuthority {
 bool allowed=true;
 bool granted(std::string& why) override {
  if(!allowed)why="Stock owner handoff revoked";
  return allowed;
 }
};
struct TestGuard:RecoveryGuard {
 bool allowed=true;
 bool verified(std::string& why) override {
  if(!allowed)why="Supervisor denied recovery";
  return allowed;
 }
};
struct TestHardware:HardwareEvidence {
 bool input=true,panel=true,restore=true,throw_input=false;
 bool input_mapping_verified(std::string& why) override {
  if(throw_input)throw std::runtime_error("mapping probe failed");
  if(!input)why="Mapping has not been confirmed";return input;
 }
 bool panel_topology_verified(std::string& why) override {
  if(!panel)why="Panel topology unresolved";return panel;
 }
 bool stock_restoration_verified(std::string& why) override {
  if(!restore)why="Stock restore not proved";return restore;
 }
};
struct TestScreen:ScreenBackend {
 int acquisitions=0,releases=0;
 bool allow=true;
 bool acquire(std::string& why) override {
  ++acquisitions;if(!allow)why="DRM lease rejected";return allow;
 }
 bool submit(const std::vector<std::uint8_t>&,std::string&) override {return true;}
 void release() noexcept override {++releases;}
};
struct Fixture {
 TempDir t;
 FakeProbe* probe=nullptr;
 std::unique_ptr<VolumeRegistry> registry;
 recovery::RecoveryControl recovery;
 recovery::LeaseToken lease;
 TestAuthority authority;
 TestGuard guard;
 TestHardware hw;
 TestScreen screen;
 DisplaySession session;
 Fixture():session(authority,screen){
  t.mkdir("sd");
  auto fp=std::make_unique<FakeProbe>();probe=fp.get();
  fp->table={{"/dev/mmcblk1p1",t.sub("sd"),"exfat","rw"}};
  auto rules=test_rules();rules[0].mount_prefix=t.sub("s");
  registry=std::make_unique<VolumeRegistry>(rules,std::move(fp));
  std::string why;
  CHECK(recovery.open(t.path,why));
  CHECK(recovery.prepare(lease,why));
  CHECK(recovery.activate(lease,why));
 }
 StartupPreflight preflight(const std::string& volume="sdcard"){
  return StartupPreflight(*registry,volume,recovery,lease,authority,guard,hw);
 }
};
}
TEST(startup_preflight_default_unverified_hardware_always_denies){
 Fixture f;UnverifiedHardware default_hw;
 StartupPreflight gate(*f.registry,"sdcard",f.recovery,f.lease,f.authority,f.guard,default_hw);
 const auto report=gate.inspect();
 CHECK(!report.ready);CHECK_EQ(report.checks.size(),7u);
 std::string why;CHECK(!gate.start_guarded(f.session,why));
 CHECK_EQ(f.screen.acquisitions,0);
 CHECK(!why.empty());
}
TEST(startup_preflight_requires_verified_storage_and_recovery){
 Fixture f;auto gate=f.preflight();std::string why;
 CHECK(gate.inspect().ready);
 f.probe->exported={"mmcblk1"};
 CHECK(!gate.inspect().ready);
 CHECK(!gate.start_guarded(f.session,why));
 CHECK_EQ(f.screen.acquisitions,0);
 f.probe->exported.clear();
 CHECK(f.recovery.revoke(why));
 CHECK(!gate.inspect().ready);
 CHECK(!gate.start_guarded(f.session,why));
 CHECK_EQ(f.screen.acquisitions,0);
 CHECK(f.recovery.record_restored(true,why));
 CHECK(!gate.inspect().ready); // old lease is revoked permanently
}
TEST(startup_preflight_invalid_or_missing_lease_denies){
 Fixture f;
 recovery::LeaseToken bad=f.lease;bad.secret[0]='z';
 StartupPreflight gate(*f.registry,"sdcard",f.recovery,bad,f.authority,f.guard,f.hw);
 std::string why;
 CHECK(!gate.inspect().ready);
 CHECK(!gate.start_guarded(f.session,why));
 CHECK_EQ(f.screen.acquisitions,0);
 auto missing=f.preflight("not-a-volume");
 CHECK(!missing.inspect().ready);
}
TEST(startup_preflight_guards_each_handoff_requirement){
 Fixture f;auto gate=f.preflight();std::string why;
 f.authority.allowed=false;
 CHECK(!gate.start_guarded(f.session,why));CHECK_EQ(f.screen.acquisitions,0);
 f.authority.allowed=true;f.guard.allowed=false;
 CHECK(!gate.start_guarded(f.session,why));CHECK_EQ(f.screen.acquisitions,0);
 f.guard.allowed=true;f.hw.restore=false;
 CHECK(!gate.start_guarded(f.session,why));CHECK_EQ(f.screen.acquisitions,0);
 f.hw.restore=true;f.hw.panel=false;
 CHECK(!gate.start_guarded(f.session,why));CHECK_EQ(f.screen.acquisitions,0);
 f.hw.panel=true;f.hw.input=false;
 CHECK(!gate.start_guarded(f.session,why));CHECK_EQ(f.screen.acquisitions,0);
 f.hw.input=true;f.hw.throw_input=true;
 CHECK(!gate.start_guarded(f.session,why));CHECK_EQ(f.screen.acquisitions,0);
}
TEST(startup_preflight_simulated_acquire_failure_and_revocation_restore){
 Fixture f;auto gate=f.preflight();std::string why;
 f.screen.allow=false;
 CHECK(!gate.start_guarded(f.session,why));
 CHECK_EQ(f.screen.acquisitions,1);
 CHECK_EQ(f.screen.releases,1);
 f.screen.allow=true;
 CHECK(gate.start_guarded(f.session,why));
 CHECK(f.session.active());
 f.hw.input=false;
 CHECK(!gate.verify_active(f.session,why));
 CHECK(!f.session.active());
 CHECK_EQ(f.screen.releases,2);
 f.hw.input=true;
 CHECK(gate.start_guarded(f.session,why));
 f.probe->exported={"mmcblk1"};
 CHECK(!gate.verify_active(f.session,why));
 CHECK(!f.session.active());
 CHECK_EQ(f.screen.releases,3);
}
