// Read-only MPC Sample startup diagnostics and evidence collection.
// Never opens DRM/ALSA hardware, stops services, or grants recovery leases.
#include "startup_preflight.h"
#include "hardware_bridge.h"
#include <fstream>
#include <iostream>
#include <string>
#include <sys/utsname.h>
int main(int argc,char** argv) {
 using namespace dm;
 using namespace dm::native;
 if(argc>2 || (argc==2 && std::string(argv[1])!="--evidence")) {
  std::cerr<<"usage: diskmode-native-diagnostics [--evidence]\n";
  return 64;
 }
 auto volumes=VolumeRegistry(VolumeRegistry::default_rules(),make_linux_probe(""));
 if(argc==2) {
  // Evidence is OBSERVATION, never proof of ownership, panel compatibility
  // or physical button mapping. No new authentication/activation path.
  json report={{"kind","diskmode-hardware-evidence-v1"},
               {"hardware_verified",false},{"ownership_granted",false},
               {"recovery_verified",false}};
  struct utsname system{};
  if(::uname(&system)==0)
   report["kernel"]={{"machine",system.machine},{"release",system.release}};
  else report["kernel"]={{"status","unavailable"}};
  const auto drm=probe_display_candidates();
  report["display"]={{"candidate_nodes",drm.nodes},
                     {"reason",drm.reason},{"panel_verified",false}};
  std::ifstream cards("/proc/asound/cards");
  std::string line;
  bool control_surface_hint=false;
  bool cards_readable=bool(cards);
  // Read a bounded amount from procfs; strings are not executed or used
  // as a device selector, and this never opens any ALSA endpoint.
  for(std::size_t n=0;cards && n<64 && std::getline(cards,line);++n)
   if(line.find("Control Surface")!=std::string::npos ||
      line.find("Surface")!=std::string::npos)
    control_surface_hint=true;
  report["input"]={{"alsa_cards_readable",cards_readable},
                   {"control_surface_hint",control_surface_hint},
                   {"mapping_verified",false}};
  report["storage"]=json::array();
  for(const auto& v:volumes.list())
   report["storage"].push_back({{"id",v.id},{"mounted",v.mounted},
         {"ownership",ownership_name(v.ownership)},
         {"fs_read_only",v.fs_read_only},{"filesystem",v.fs_type}});
  report["note"]="Read-only observations only; no verified physical handoff";
  std::cout<<report.dump()<<'\n';
  return 0;
 }
 recovery::RecoveryControl recovery; // intentionally unopened
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
 return report.ready?0:2;
}
