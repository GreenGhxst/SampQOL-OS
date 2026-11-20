// Shared File Operations. Used by the HTTP API, the job queue, the transfer
// manager and the inbox. All paths are (VolumeInfo, RelPath); the actual
// work is done on descriptors returned by PathAuthorizer.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../util/common.h"
#include "journal.h"
#include "path_auth.h"
#include "volume_registry.h"

namespace dm {

struct Entry {
    std::string name;
    std::string type;        // "file" | "dir" | "other"
    std::uint64_t size = 0;
    std::int64_t mtime = 0;  // unix seconds
    bool is_protected = false;
    json to_json() const;
};

struct ListOptions {
    std::size_t offset = 0;
    std::size_t limit = 500;       // incremental enumeration; server caps this
    std::string sort = "name";     // name | size | mtime | type
    bool descending = false;
    std::string filter;            // case-insensitive substring on name
    std::string kind;              // "" | "audio" | "project" | "dir" | "file"
    bool show_hidden = false;
};

struct Listing {
    std::vector<Entry> entries;
    std::size_t total = 0;         // after filtering
    bool truncated = false;
};

enum class Conflict { fail, rename, overwrite };
Conflict conflict_from_string(const std::string& s);

using ProgressFn = std::function<void(std::uint64_t done, std::uint64_t total)>;

struct OpContext {
    std::atomic<bool>* cancel = nullptr;
    ProgressFn progress;
    bool cancelled() const { return cancel && cancel->load(); }
};

struct TrashItem {
    std::string id, name, original_path;
    std::uint64_t size = 0;
    std::string deleted_at;
    json to_json() const;
};

std::string file_kind(const std::string& name);   // "audio" | "project" | "other" from the extension

class FileOps {
public:
    FileOps(Journal& journal, std::uint64_t free_space_margin = 8ull * 1024 * 1024);

    Status list(const VolumeInfo& v, const RelPath& dir, const ListOptions& o, Listing& out);
    Status stat(const VolumeInfo& v, const RelPath& p, Entry& out);
    Status mkdir(const VolumeInfo& v, const RelPath& p);
    Status rename(const VolumeInfo& v, const RelPath& p, const std::string& new_name);
    // Copies file or directory `src` into directory `dst_dir` as `name`
    // (empty: keep the source name). Cross-volume aware.
    Status copy(const VolumeInfo& sv, const RelPath& src, const VolumeInfo& dv, const RelPath& dst_dir,
                const std::string& name, Conflict c, const OpContext& ctx, std::string* final_name = nullptr);
    Status move(const VolumeInfo& sv, const RelPath& src, const VolumeInfo& dv, const RelPath& dst_dir,
                const std::string& name, Conflict c, const OpContext& ctx, std::string* final_name = nullptr);
    // Moves to the volume's trash (".diskmode/trash") when possible.
    // `permanent` removes immediately; it is never the default.
    Status remove(const VolumeInfo& v, const RelPath& p, bool permanent, std::string* trash_id = nullptr);
    Status list_trash(const VolumeInfo& v, std::vector<TrashItem>& out);
    Status restore(const VolumeInfo& v, const std::string& trash_id, std::string* restored_path = nullptr);
    Status purge_trash_item(const VolumeInfo& v, const std::string& trash_id);
    Status sha256(const VolumeInfo& v, const RelPath& p, std::string& hex, const OpContext& ctx);
    // Opens a file for streaming reads (download/preview). Caller owns fd.
    Status open_read(const VolumeInfo& v, const RelPath& p, ScopedFd& fd, std::uint64_t& size);
    // Atomically places an already-written staging file (absolute path, on
    // any filesystem) at dst_dir/name. Used by uploads and the inbox.
    Status place_file(const std::string& staged_abs, const VolumeInfo& dv, const RelPath& dst_dir,
                      const std::string& name, Conflict c, const OpContext& ctx, std::string* final_name = nullptr);
    // Free space on the volume (bytes) minus the safety margin.
    std::uint64_t usable_free(const VolumeInfo& v) const;
    // Counts files/folders and bytes under a directory (bounded walk).
    Status usage(const VolumeInfo& v, const RelPath& dir, std::uint64_t& files, std::uint64_t& dirs,
                 std::uint64_t& bytes, std::size_t max_entries = 200000);

    static Status errno_status(const char* what);
    static std::string unique_name(int dirfd, const std::string& name); // "name 2.ext" style

private:
    Status copy_tree(int sdir, const std::string& sname, int ddir, const std::string& dname,
                     const OpContext& ctx, std::uint64_t& done, std::uint64_t total, std::vector<std::string>& temps,
                     const std::string& dabs, bool overwrite);
    Status copy_regular(int sdir, const std::string& sname, int ddir, const std::string& dname,
                        const OpContext& ctx, std::uint64_t& done, std::uint64_t total, const std::string& dabs,
                        std::vector<std::string>& temps, bool overwrite);
    Status remove_tree(int dir, const std::string& name);
    Status tree_size(int dir, const std::string& name, std::uint64_t& bytes, std::uint64_t& count, std::size_t limit);
    Status open_trash(const VolumeInfo& v, ScopedFd& trash, bool create);
    Status read_trash_index(int trashfd, json& idx);
    Status write_trash_index(int trashfd, const json& idx);
    Status pick_destination(int ddir, const std::string& dabs, const std::string& wanted, Conflict c,
                            std::string& final_name, bool& overwrite);

    Journal& journal_;
    std::uint64_t margin_;
};

} // namespace dm
