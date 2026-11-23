#pragma once
// DISKMODE SIMULATION-ONLY recovery supervisor.
// No production RecoveryGuard, systemd access, DRM node, or stock service writes.
#include <string>
#include <cstdint>
namespace dm::native::recovery {
enum class State { idle, stock_stopped, child_running, revoking, restoring, recovered, failed };
struct MockStock {
    bool running = true;
    bool allow_stop = true;
    bool allow_restore = true;
    unsigned stops = 0;
    unsigned restores = 0;
    bool stop(std::string& error);
    bool restore(std::string& error);
};
class Supervisor {
public:
    explicit Supervisor(MockStock& stock):stock_(stock){}
    // Simulate a verified external handoff. Only the *simulation* state changes.
    bool begin(std::string& error);
    bool child_ready(std::string& error);
    void revoke();
    // Called after waitpid has confirmed the child exited; always restore stock.
    bool child_exited(std::string& error);
    // Prevent a failed stock restoration from yielding a new lease.
    bool lease_valid() const { return state_ == State::child_running; }
    State state() const { return state_; }
    unsigned restarts() const { return stock_.restores; }
private:
    MockStock& stock_;
    State state_ = State::idle;
};
const char* state_name(State s);
} // namespace dm::native::recovery
