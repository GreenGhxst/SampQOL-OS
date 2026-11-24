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
   report["kernel"]={{"machine",system.machine}}; // Avoid exposing a kernel-build fingerprint
  else report["kernel"]={{"status","unavailable"}};
  const auto drm=probe_display_candidates();
  report["display"]={{"candidate_nodes",drm.nodes},
                     {"reason",drm.reason},{"panel_verified",false}};
  std::ifstream cards("/proc/asound/cards");
  std::string line;
  bool control_surface_hint=false;
  const bool cards_readable=bool(cards);
  std::size_t cards_lines=0;
  // Bounded procfs read: a name match is only a hint, never verified input.
  for(;cards_lines<64 && std::getline(cards,line);++cards_lines)
   if(line.find("Control Surface")!=std::string::npos)
    control_surface_hint=true;
  const bool cards_truncated=cards_readable && cards_lines==64 && bool(std::getline(cards,line));
  report["input"]={{"alsa_cards_readable",cards_readable},
                   {"alsa_cards_truncated",cards_truncated},
                   {"control_surface_hint",cards_readable ?
                       json(control_surface_hint) : json(nullptr)},
                   {"mapping_verified",false}};
  report["storage"]=json::array();
  for(const auto& v:volumes.list()) {
   // The existing registry cannot distinguish "not USB-exported" from
   // "configfs unreadable". Never translate that into verified ownership.
   const char* observed=v.ownership==Ownership::usb_host?"usb-export-detected":
                        v.ownership==Ownership::unmounted?"unmounted":
                        "unknown";
   report["storage"].push_back({{"id",v.id},{"mounted",v.mounted},
         {"ownership_observation",observed},{"ownership_verified",false},
         {"fs_read_only",v.mounted ? json(v.fs_read_only) : json(nullptr)},
         {"filesystem",v.mounted ? json(v.fs_type) : json(nullptr)}});
  }
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
