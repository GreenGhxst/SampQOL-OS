#pragma once
// Queued native operations share Web Console authorization and job machinery.
// Queue and core dependencies must outlive NativeJobs; destructor waits until
// every started native worker stops using those dependencies.
#include "core/file_ops.h"
#include "core/job_queue.h"
#include "core/volume_registry.h"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
namespace dm::native {
enum class NativeOperation { copy, move, trash };
struct NativeRequest {
    NativeOperation op=NativeOperation::copy;
    std::string source_volume,source_path,destination_volume,destination_dir;
    bool confirmed=false;
};
class NativeJobs {
public:
    NativeJobs(VolumeRegistry& volumes,FileOps& files,JobQueue& queue);
    ~NativeJobs();
    NativeJobs(const NativeJobs&)=delete;
    NativeJobs& operator=(const NativeJobs&)=delete;
    Status submit(const NativeRequest& request,std::string& job_id);
private:
    struct SharedState {
        VolumeRegistry& volumes;
        FileOps& files;
        JobQueue& queue;
        std::mutex mu;
        std::condition_variable cv;
        bool closing=false;
        std::size_t running=0;
        SharedState(VolumeRegistry& v,FileOps& f,JobQueue& q):volumes(v),files(f),queue(q){}
    };
    std::shared_ptr<SharedState> state_;
};
} // namespace dm::native
