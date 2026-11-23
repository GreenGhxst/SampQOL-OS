#pragma once
// P0 native composition. Physical presentation requires an independently
// verified DisplaySession. The mock controller offers explicit review/cancel.
#include "device_session.h"
#include "mock_inputs.h"
#include "native_jobs.h"
#include "native_renderer.h"
#include <optional>
#include <string>
#include <vector>
namespace dm::native {
class NativeRuntime {
public:
    explicit NativeRuntime(Service& service,NativeJobs* jobs=nullptr,DisplaySession* display=nullptr)
      : controller_(service),jobs_(jobs),display_(display) {}
    Controller& controller(){return controller_;}
    const Controller& controller() const{return controller_;}
    NativeRenderer::Frame frame() const;
    // During confirmation only wheel_press (approve) and back (cancel) act.
    bool mock_press(MockButton key);
    bool awaiting_confirmation() const{return pending_.has_value();}
    // Stage an immutable review from exactly one selected visible item.
    Status prepare_selected(NativeOperation op,const std::string& target_volume,
                            const std::string& target_dir);
    // The last approval result can be inspected for failure and job tracking.
    const Status& last_result() const{return last_result_;}
    const std::string& last_job_id() const{return last_job_id_;}
    bool start_display(std::string& reason);
    bool present(std::string& reason);
    void stop_display() noexcept;
    void refresh(){if(!pending_)controller_.press(Key::refresh);}
private:
    Controller controller_;
    NativeRenderer renderer_;
    NativeJobs* jobs_=nullptr; // core owner must outlive this runtime
    DisplaySession* display_=nullptr; // independent verified supervisor
    std::optional<NativeRequest> pending_;
    Status last_result_=Status::good();
    std::string last_job_id_;
};
} // namespace dm::native
