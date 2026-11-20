// Transfer Manager: chunked, resumable uploads into a staging area with a
// running SHA-256, committed atomically to their destination through FileOps
// (or handed to the Smart Inbox). Sizes are bounded; stale uploads are
// reaped.
#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "../util/common.h"
#include "../util/sha256.h"
#include "file_ops.h"
#include "path_auth.h"

namespace dm {

struct Upload {
    std::string id, owner, volume, dir, name, expected_sha256, staging_path, state = "open", error;
    std::uint64_t size = 0, received = 0;
    std::int64_t created = 0, updated = 0;
    bool inbox = false;                 // commit into the inbox instead of a volume path
    std::unique_ptr<Sha256> hash;
    std::string final_sha256;
    json to_json() const;
};

class TransferManager {
public:
    TransferManager(std::string staging_dir, std::uint64_t max_upload_bytes, std::size_t max_open);
    Status begin(const std::string& owner, const std::string& volume, const std::string& dir, const std::string& name,
                 std::uint64_t size, const std::string& expected_sha256, bool inbox, std::string& id_out);
    // Appends a chunk; `offset` must equal the bytes received so far.
    Status append(const std::string& id, const std::string& owner, std::uint64_t offset, const char* data, std::size_t len);
    // Verifies size/hash. On success the staged file is complete and
    // `staged_path`/`sha256` are filled; the caller places it.
    Status finish(const std::string& id, const std::string& owner, std::string& staged_path, std::string& sha256);
    // For an upload that already finished (e.g. a commit that hit a name
    // conflict): returns the staged path and digest again.
    Status completed(const std::string& id, const std::string& owner, std::string& staged_path, std::string& sha256) const;
    Status abort(const std::string& id, const std::string& owner);
    json get(const std::string& id, const std::string& owner) const;
    json list(const std::string& owner) const;
    void forget(const std::string& id);          // after a successful placement
    std::size_t reap_stale(std::int64_t older_than_ms);   // removes abandoned staging files
    const std::string& staging_dir() const { return staging_dir_; }
    std::uint64_t max_upload_bytes() const { return max_bytes_; }

private:
    std::string staging_dir_;
    std::uint64_t max_bytes_;
    std::size_t max_open_;
    mutable std::mutex mu_;
    std::map<std::string, std::unique_ptr<Upload>> uploads_;
};

} // namespace dm
