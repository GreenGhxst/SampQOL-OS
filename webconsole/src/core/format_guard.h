// Guarded microSD formatting.
//
// Real formatting is NOT implemented in this build. The guard runs the full
// preflight, issues a single-use token bound to (volume, device, session) and
// executes through a FormatExecutor interface whose only implementation is a
// non-destructive simulator. A production executor must be a separately
// audited privileged helper that accepts a validated volume identifier only.
#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../util/common.h"
#include "job_queue.h"
#include "volume_registry.h"

namespace dm {

struct BlockDevice {
    std::string name;            // mmcblk1
    bool removable = false;
    std::string media;           // "flash_sd" when known (edisksd), else ""
    std::uint64_t size_bytes = 0;
    std::vector<std::string> partitions;   // mmcblk1p1
    bool is_system = false;      // holds / , /data, /content etc.
    json to_json() const;
};

struct BlockDeviceInventory {
    virtual ~BlockDeviceInventory() = default;
    virtual std::vector<BlockDevice> list() = 0;
};
std::unique_ptr<BlockDeviceInventory> make_linux_block_inventory();
// Test double: fixed inventory.
std::unique_ptr<BlockDeviceInventory> make_fake_block_inventory(std::vector<BlockDevice> devices);

struct FormatRequest { std::string volume_id, device, fs_type, label; };

struct FormatExecutor {
    virtual ~FormatExecutor() = default;
    virtual bool is_simulation() const = 0;
    virtual Status execute(const FormatRequest& r, json& result) = 0;
};
std::unique_ptr<FormatExecutor> make_simulated_format_executor();

struct PreflightCheck { std::string name, detail; bool pass = false; json to_json() const; };

class FormatGuard {
public:
    FormatGuard(VolumeRegistry& reg, JobQueue& jobs, std::unique_ptr<BlockDeviceInventory> inv,
                std::unique_ptr<FormatExecutor> exec, std::vector<std::string> supported_fs = {"exfat"});
    // Runs every check. Issues a token only when all pass.
    json preflight(const std::string& volume_id, const std::string& session_id);
    // Consumes the token; re-runs preflight; executes via the executor.
    json confirm(const std::string& token, const std::string& volume_id, const std::string& device,
                 const std::string& fs_type, const std::string& label, const std::string& session_id);
    bool simulation() const { return exec_->is_simulation(); }
    json devices();

private:
    std::vector<PreflightCheck> run_checks(const std::string& volume_id, std::string& device_out, std::uint64_t& size_out,
                                           const std::string& fs_type);
    struct Token { std::string volume, device, session; std::int64_t expires = 0; };
    VolumeRegistry& reg_;
    JobQueue& jobs_;
    std::unique_ptr<BlockDeviceInventory> inv_;
    std::unique_ptr<FormatExecutor> exec_;
    std::vector<std::string> supported_fs_;
    std::mutex mu_;
    std::map<std::string, Token> tokens_;
    bool in_progress_ = false;
};

} // namespace dm
