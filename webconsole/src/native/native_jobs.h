#pragma once
// Explicit-confirmation native file jobs sharing the Web Console's FileOps,
// authorization, journal and JobQueue. Caller must keep dependencies alive.
#include "core/file_ops.h"
#include "core/job_queue.h"
#include "core/volume_registry.h"
#include <string>
namespace dm::native {
enum class NativeOperation { copy, move, trash };
struct NativeRequest {
    NativeOperation op=NativeOperation::copy;
    std::string source_volume,source_path,destination_volume,destination_dir;
    // Must be true for each request; never inferred from a keypress.
    bool confirmed=false;
};
class NativeJobs {
public:
    NativeJobs(VolumeRegistry& volumes,FileOps& files,JobQueue& queue)
       : volumes_(volumes),files_(files),queue_(queue) {}
    Status submit(const NativeRequest& request,std::string& job_id);
private:
    VolumeRegistry& volumes_;
    FileOps& files_;
    JobQueue& queue_;
};
} // namespace dm::native
