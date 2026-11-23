#pragma once
// Durable recovery control plane, simulation-only until a real stock handoff,
// persistent trusted supervisor and device recovery are independently verified.
// This module never implements a production RecoveryGuard or touches DRM/systemd.
#include <cstdint>
#include <string>
namespace dm::native::recovery {
enum class LeasePhase { idle, prepared, active, revoked, recovery_pending, recovered, failed };
struct LeaseToken {
    std::string id;       // nonsensitive, journaled identifier
    std::string secret;   // ephemeral capability, NEVER written to disk
};
class RecoveryControl {
public:
    RecoveryControl() = default;
    ~RecoveryControl();
    RecoveryControl(const RecoveryControl&) = delete;
    RecoveryControl& operator=(const RecoveryControl&) = delete;
    // Directory must exist and be owner-only (0700); no symlink traversal.
    bool open(const std::string& directory, std::string& error);
    // A restart ALWAYS invalidates outstanding capabilities. Incomplete
    // sessions become recovery_pending and require verified restoration.
    bool prepare(LeaseToken& token, std::string& error);
    bool activate(const LeaseToken& token, std::string& error);
    bool authenticate(const LeaseToken& token) const;
    bool revoke(std::string& error);
    bool record_restored(bool success, std::string& error);
    LeasePhase phase() const { return phase_; }
    bool needs_restoration() const { return phase_ == LeasePhase::recovery_pending || phase_ == LeasePhase::revoked || phase_ == LeasePhase::failed; }
    unsigned long long sequence() const { return sequence_; }
    void close() noexcept;
private:
    bool append(LeasePhase next, const std::string& lease_id, std::string& error);
    int dir_fd_=-1, lock_fd_=-1, journal_fd_=-1;
    LeasePhase phase_=LeasePhase::idle;
    unsigned long long sequence_=0;
    LeaseToken current_;
};
const char* lease_phase_name(LeasePhase);
} // namespace dm::native::recovery
