// Read-only host/device startup report. Deliberately cannot start any
// display/input session and returns blocked status until separate hardware
// verification and authenticated recovery authority exist.
#include "startup_preflight.h"
#include <iostream>
int main() {
 using namespace dm;
 using namespace dm::native;
 auto volumes=VolumeRegistry(VolumeRegistry::default_rules(),make_linux_probe(""));
 recovery::RecoveryControl recovery; // intentionally unopened: NEVER mint a lease
 recovery::LeaseToken no_lease;
 RefuseOwnership no_owner;
 NoRecoveryGuard no_guard;
 UnverifiedHardware unverified;
 StartupPreflight gate(volumes,"sdcard",recovery,no_lease,no_owner,no_guard,unverified);
 const auto report=gate.inspect();
 std::cout<<"DISKMODE P0 startup diagnostics (READ ONLY / NO DEVICE HANDOFF)\n";
 for(const auto& check:report.checks)
  std::cout<<(check.passed?"PASS ":"BLOCK ")<<check.name<<" : "<<check.detail<<"\n";
 std::cout<<"RESULT: "<<(report.ready?"ready":"blocked")<<"\n";
 // This utility intentionally provides no interactive override or hardware
 // activation path, even if storage mounts are present.
 return report.ready?0:2;
}
