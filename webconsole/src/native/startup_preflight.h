#pragma once
// P0 Phase 3: read-only startup readiness checks. This is NOT a production
// stock takeover or a mechanism for granting hardware capabilities.
// Tests may provide simulated evidence; product defaults always deny.
#include "device_session.h"
#include "kms_backend.h"
#include "recovery_control.h"
#include "core/volume_registry.h"
#include <string>
#include <vector>
namespace dm::native {
struct HardwareEvidence {
    virtual ~HardwareEvidence()=default;
    // Must originate from separately verified device-side evidence, never
    // guessed from device existence or a provisional MIDI mapping.
    virtual bool input_mapping_verified(std::string& reason)=0;
    virtual bool panel_topology_verified(std::string& reason)=0;
    virtual bool stock_restoration_verified(std::string& reason)=0;
};
struct UnverifiedHardware final : HardwareEvidence {
    bool input_mapping_verified(std::string& reason) override {
        reason="MPC Sample physical control mappings not verified";return false;
    }
    bool panel_topology_verified(std::string& reason) override {
        reason="MPC Sample panel and DRM ownership not verified";return false;
    }
    bool stock_restoration_verified(std::string& reason) override {
        reason="No independently verified stock-app restoration";return false;
    }
};
struct StartupCheck {
    std::string name;
    bool passed=false;
    std::string detail;
};
struct StartupReport {
    bool ready=false;
    std::vector<StartupCheck> checks;
    std::string summary() const;
};
class StartupPreflight {
public:
    StartupPreflight(VolumeRegistry& volumes,std::string volume_id,
                     recovery::RecoveryControl& recovery,const recovery::LeaseToken& lease,
                     OwnershipAuthority& owner,RecoveryGuard& guard,HardwareEvidence& hardware)
      :volumes_(volumes),volume_id_(std::move(volume_id)),recovery_(recovery),
       lease_(lease),owner_(owner),guard_(guard),hardware_(hardware) {}
    // Read-only checks. No ALSA open, DRM master, service shutdown, formatting,
    // stock firmware modification, or durable recovery transition.
    StartupReport inspect();
    // No display acquisition unless EVERY check passed. Rechecks after start
    // and rolls back session on any revocation or failed subsequent check.
    bool start_guarded(DisplaySession& display,std::string& reason);
    bool verify_active(DisplaySession& display,std::string& reason);
private:
    VolumeRegistry& volumes_;
    std::string volume_id_;
    recovery::RecoveryControl& recovery_;
    recovery::LeaseToken lease_;
    OwnershipAuthority& owner_;
    RecoveryGuard& guard_;
    HardwareEvidence& hardware_;
};
} // namespace dm::native
