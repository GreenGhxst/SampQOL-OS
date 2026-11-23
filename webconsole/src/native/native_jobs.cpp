#include "native_jobs.h"
#include "core/path_auth.h"
namespace dm::native {
NativeJobs::NativeJobs(VolumeRegistry& volumes,FileOps& files,JobQueue& queue)
  :state_(std::make_shared<SharedState>(volumes,files,queue)) {}
NativeJobs::~NativeJobs() {
    auto state=state_;
    std::unique_lock<std::mutex> lk(state->mu);
    state->closing=true;
    state->cv.wait(lk,[&]{return state->running==0;});
    // Queued callbacks hold only state, not this. They detect closing and
    // cancel themselves without dereferencing core dependencies.
}
Status NativeJobs::submit(const NativeRequest& r,std::string& job_id) {
    job_id.clear();
    auto state=state_;
    if(!r.confirmed)return Status::fail(Err::forbidden,"Explicit confirmation required");
    if(r.source_volume.empty() || r.source_path.empty())
        return Status::fail(Err::invalid_argument,"A source file or folder is required");
    RelPath src,dst;
    auto st=parse_rel_path(r.source_path,src);if(!st.ok())return st;
    if(src.empty())return Status::fail(Err::forbidden,"Volume root operations are prohibited");
    if(r.op!=NativeOperation::trash) {
        st=parse_rel_path(r.destination_dir,dst);if(!st.ok())return st;
        if(r.destination_volume.empty())return Status::fail(Err::invalid_argument,"Destination volume missing");
        if(r.source_volume==r.destination_volume && src.parent().str()==dst.str())
            return Status::fail(Err::invalid_argument,"Source and destination are identical");
        // Never allow moving/copying a folder into itself or its descendants.
        if(r.source_volume==r.destination_volume && dst.starts_with(src))
            return Status::fail(Err::invalid_argument,"Destination is within source");
    }
    state->volumes.refresh();
    const auto source=state->volumes.get(r.source_volume);
    if(!source || !source->mounted || source->ownership!=Ownership::device)
        return Status::fail(Err::unavailable,"Source volume is unavailable");
    st=PathAuthorizer::check_read(*source);if(!st.ok())return st;
    if(PathAuthorizer::is_protected(*source,src))
        return Status::fail(Err::forbidden,"Protected source");
    if(r.op!=NativeOperation::copy) {
        st=PathAuthorizer::check_write(*source,src);if(!st.ok())return st;
    }
    if(r.op!=NativeOperation::trash) {
        const auto destination=state->volumes.get(r.destination_volume);
        if(!destination || !destination->mounted || destination->ownership!=Ownership::device)
            return Status::fail(Err::unavailable,"Destination volume unavailable");
        st=PathAuthorizer::check_write(*destination,dst);if(!st.ok())return st;
    }
    const std::string type=r.op==NativeOperation::copy?"native-copy":
                           r.op==NativeOperation::move?"native-move":"native-trash";
    const std::string target=r.op==NativeOperation::trash?"":r.destination_volume+":"+r.destination_dir;
    // Immutable request value, no captured pointers to transient UI objects.
    std::lock_guard<std::mutex> lock(state->mu);
    if(state->closing)return Status::fail(Err::unavailable,"Native jobs shutting down");
    return state->queue.submit(type,"native",[state,r](Job& job) {
        {
            std::lock_guard<std::mutex> lk(state->mu);
            if(state->closing){job.cancel=true;return;}
            ++state->running;
        }
        struct Completion {
            std::shared_ptr<SharedState> state;
            ~Completion() {
                std::lock_guard<std::mutex> lk(state->mu);
                --state->running;
                state->cv.notify_all();
            }
        } completion{state};
        if(job.cancel){state->queue.set_item(job,0,"cancelled");return;}
        state->volumes.refresh();
        auto sv=state->volumes.get(r.source_volume);
        if(!sv || !sv->mounted || sv->ownership!=Ownership::device) {
            state->queue.set_item(job,0,"failed","Source volume revoked");return;
        }
        RelPath from,to;
        auto status=parse_rel_path(r.source_path,from);
        if(!status.ok()){state->queue.set_item(job,0,"failed",status.message);return;}
        OpContext context;
        context.cancel=&job.cancel;
        context.progress=[&job](std::uint64_t done,std::uint64_t total) {
            job.done.store(done);job.total.store(total);
        };
        if(r.op==NativeOperation::trash) {
            status=state->files.remove(*sv,from,false);
        } else {
            auto dv=state->volumes.get(r.destination_volume);
            if(!dv || !dv->mounted || dv->ownership!=Ownership::device) {
                state->queue.set_item(job,0,"failed","Destination volume revoked");return;
            }
            status=parse_rel_path(r.destination_dir,to);
            if(status.ok()) {
                // Conflict::fail refuses overwrite; shared FileOps handles journal,
                // fd-relative path checks and rollback of interrupted copies.
                if(r.op==NativeOperation::copy)
                    status=state->files.copy(*sv,from,*dv,to,"",Conflict::fail,context);
                else status=state->files.move(*sv,from,*dv,to,"",Conflict::fail,context);
            }
        }
        state->queue.set_item(job,0,status.ok()?"done":(status.code==Err::partial?"failed":(job.cancel?"cancelled":"failed")),status.message);
    },job_id,{JobItem{r.source_volume+":"+r.source_path,target,"queued",""}});
}
} // namespace dm::native
