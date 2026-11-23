#include "native_runtime.h"
#include "core/path_auth.h"
namespace dm::native {
bool NativeRuntime::start_display(std::string& reason) {
    if(!display_){reason="No verified display session installed";return false;}
    return display_->start(reason);
}
bool NativeRuntime::present(std::string& reason) {
    if(!display_ || !display_->active()){reason="No active verified display session";return false;}
    return display_->present(frame(),reason);
}
void NativeRuntime::stop_display() noexcept {
    if(display_)display_->stop();
}
Status NativeRuntime::submit_selected(NativeOperation op,const std::string& target_volume,
                                      const std::string& target_dir,bool confirmed,std::string& job_id) {
    job_id.clear();
    if(!confirmed)return Status::fail(Err::forbidden,"Explicit confirmation required");
    if(!jobs_)return Status::fail(Err::unsupported,"Native jobs unavailable");
    if(controller_.screen()!=Screen::files || controller_.selected_count()!=1)
        return Status::fail(Err::invalid_argument,"Select exactly one file or directory");
    std::string selected;
    for(std::size_t i=0;i<controller_.entries().size();++i) {
        if(controller_.selected(i)){selected=controller_.entries()[i].name;break;}
    }
    if(selected.empty())return Status::fail(Err::invalid_argument,"Selected item unavailable");
    const std::string relative=controller_.directory().empty()?selected:
                              controller_.directory()+"/"+selected;
    RelPath validated;
    auto st=parse_rel_path(relative,validated);if(!st.ok())return st;
    NativeRequest request;
    request.op=op;
    request.source_volume=controller_.volume_id();
    request.source_path=validated.str();
    request.destination_volume=target_volume;
    request.destination_dir=target_dir;
    request.confirmed=true;
    return jobs_->submit(request,job_id);
}
} // namespace dm::native
