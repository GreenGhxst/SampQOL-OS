#pragma once
// DRM/KMS presentation behind an independent, verified recovery supervisor.
// The default guard DENIES all physical display acquisition. Tests inject
// fake drivers and guards; no public device-launch path exists.
#include "device_session.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace dm::native {
struct RecoveryGuard {
    virtual ~RecoveryGuard()=default;
    // External, separately supervised owner handoff AND crash recovery must
    // already have been verified. Never infer this from uid=0 / a DRM node.
    virtual bool verified(std::string& reason)=0;
};
struct NoRecoveryGuard final : RecoveryGuard {
    bool verified(std::string& reason) override {
        reason="External stock-app recovery supervisor not installed or verified";
        return false;
    }
};
struct KmsDriver {
    virtual ~KmsDriver()=default;
    virtual bool open_and_find_panel(std::string&)=0;
    virtual bool save_previous_state(std::string&)=0;
    virtual bool acquire_master(std::string&)=0;
    virtual bool allocate_xrgb8888(std::string&)=0;
    virtual bool activate(std::string&)=0;
    virtual bool blit(const std::vector<std::uint8_t>&,std::string&)=0;
    // No-throw and idempotent. Must restore old CRTC before dropping master,
    // then delete FB, unmap/destroy dumb buffer and close DRM fd.
    virtual void restore_and_close() noexcept=0;
};
class KmsDisplayBackend final : public ScreenBackend {
public:
    KmsDisplayBackend(RecoveryGuard& guard,KmsDriver& driver)
      : guard_(guard),driver_(driver) {}
    ~KmsDisplayBackend() override {release();}
    KmsDisplayBackend(const KmsDisplayBackend&)=delete;
    KmsDisplayBackend& operator=(const KmsDisplayBackend&)=delete;
    bool acquire(std::string& reason) override;
    bool submit(const std::vector<std::uint8_t>& frame,std::string& reason) override;
    void release() noexcept override;
    bool acquired() const {return active_;}
private:
    RecoveryGuard& guard_;
    KmsDriver& driver_;
    bool active_=false;
};
// Optional libdrm implementation; available only when -DDM_WITH_DRM=ON.
// Factory always returns a backend with NoRecoveryGuard in the current
// product, so no production caller can seize the stock display.
std::unique_ptr<KmsDriver> make_libdrm_driver(std::string path="/dev/dri/card0");
} // namespace dm::native
