#pragma once
// P0 Phase 2 composition root. Host mock input and CPU render are always
// available; physical display requires a separately verified DisplaySession.
// No stock-service takeover, DRM opening, or live device controls here.
#include "device_session.h"
#include "mock_inputs.h"
#include "native_jobs.h"
#include "native_renderer.h"
namespace dm::native {
class NativeRuntime {
public:
    explicit NativeRuntime(Service& service,NativeJobs* jobs=nullptr,DisplaySession* display=nullptr)
      : controller_(service),jobs_(jobs),display_(display) {}
    Controller& controller(){return controller_;}
    const Controller& controller() const{return controller_;}
    NativeRenderer::Frame frame() const{return renderer_.render(controller_);}
    // Test/desktop path only: no direct hardware input access.
    bool mock_press(MockButton key){return mock_.dispatch(key,controller_);}
    // Physical display remains fail-closed unless a guarded session was
    // explicitly supplied and successfully acquired.
    bool start_display(std::string& reason);
    bool present(std::string& reason);
    void stop_display() noexcept;
    // Jobs are built from a SINGLE explicitly selected visible file.
    // Never accept an arbitrary source path from the UI caller.
    Status submit_selected(NativeOperation op,const std::string& target_volume,
                           const std::string& target_dir,bool confirmed,std::string& job_id);
    // File operations are asynchronous. Call after job completion for new list.
    void refresh(){controller_.press(Key::refresh);}
private:
    Controller controller_;
    NativeRenderer renderer_;
    MockInputs mock_;
    NativeJobs* jobs_=nullptr; // owned by application's core, outlives runtime
    DisplaySession* display_=nullptr; // independently supervised, outlives runtime
};
} // namespace dm::native
