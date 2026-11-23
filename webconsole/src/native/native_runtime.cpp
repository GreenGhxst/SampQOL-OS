#include "native_runtime.h"
#include "core/path_auth.h"
#include <algorithm>
namespace dm::native {
namespace {
bool displayable(const std::string& s){
    // Never approve an action whose identifying path cannot fit the native
    // confirmation text or would be drawn with ambiguous fallback glyphs.
    return s.size()<=35 && std::all_of(s.begin(),s.end(),[](unsigned char ch) {
        return ch>=32 && ch<=126;
    });
}
const char* op_name(NativeOperation op){
    return op==NativeOperation::copy?"COPY":op==NativeOperation::move?"MOVE":"TRASH";
}
}
NativeRenderer::Frame NativeRuntime::frame() const {
    if(!pending_)return renderer_.render(controller_);
    const auto& r=*pending_;
    const std::string src=r.source_volume+":"+r.source_path;
    const std::string dst=r.op==NativeOperation::trash?"RECOVERY TRASH":
                          r.destination_volume+":"+r.destination_dir;
    return renderer_.render(controller_,{
        std::string(op_name(r.op))+" / NO OVERWRITE",
        "FROM "+src,
        "TO "+dst,
        "ENTER CONFIRM  BACK CANCEL"
    });
}
bool NativeRuntime::mock_press(MockButton key) {
    if(!pending_)return mock_.dispatch(key,controller_);
    if(key==MockButton::back){
        pending_.reset();
        last_job_id_.clear();
        last_result_=Status::fail(Err::cancelled,"User cancelled confirmation");
        return true;
    }
    if(key!=MockButton::wheel_press)return false;
    auto request=*pending_;
    pending_.reset();
    last_job_id_.clear();
    if(!jobs_){
        last_result_=Status::fail(Err::unsupported,"Native jobs unavailable");
    } else {
        request.confirmed=true;
        last_result_=jobs_->submit(request,last_job_id_);
    }
    return true;
}
Status NativeRuntime::prepare_selected(NativeOperation op,const std::string& target_volume,
                                       const std::string& target_dir) {
    if(pending_)return Status::fail(Err::busy,"Finish or cancel current review");
    if(!jobs_)return Status::fail(Err::unsupported,"Native jobs unavailable");
    if(controller_.screen()!=Screen::files || controller_.selected_count()!=1)
        return Status::fail(Err::invalid_argument,"Select exactly one file or directory");
    std::string selected;
    for(std::size_t i=0;i<controller_.entries().size();++i)
        if(controller_.selected(i)){selected=controller_.entries()[i].name;break;}
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
    const std::string source_display=request.source_volume+":"+request.source_path;
    const std::string destination_display=op==NativeOperation::trash?"RECOVERY TRASH":
                                           request.destination_volume+":"+request.destination_dir;
    if(!displayable(source_display)||!displayable(destination_display))
        return Status::fail(Err::invalid_argument,
            "Path cannot be shown completely on 320x240 confirmation screen");
    pending_=std::move(request);
    last_job_id_.clear();
    last_result_=Status::good();
    return Status::good();
}
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
} // namespace dm::native
