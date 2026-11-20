// Path Authorization.
//
// Every client path is (volume id, relative path). Relative paths are parsed
// into components, validated and then walked one component at a time with
// file-descriptor-relative opens that refuse symbolic links. The result is a
// directory descriptor plus a leaf name, so file operations never re-resolve
// a textual path (avoids time-of-check/time-of-use races).
#pragma once
#include <string>
#include <sys/stat.h>
#include <vector>
#include "../util/common.h"
#include "volume_registry.h"

namespace dm {

class ScopedFd {
public:
    ScopedFd() = default;
    explicit ScopedFd(int fd) : fd_(fd) {}
    ~ScopedFd();
    ScopedFd(ScopedFd&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    ScopedFd& operator=(ScopedFd&& o) noexcept;
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    int release() { int f = fd_; fd_ = -1; return f; }
    void reset(int fd = -1);
private:
    int fd_ = -1;
};

struct RelPath {
    std::vector<std::string> parts;
    std::string str() const;              // "a/b/c" ("" for the root)
    bool empty() const { return parts.empty(); }
    RelPath parent() const;
    std::string leaf() const { return parts.empty() ? std::string() : parts.back(); }
    RelPath child(const std::string& name) const;
    bool starts_with(const RelPath& prefix) const;
};

// Validates a client-supplied relative path. Refuses absolute paths, "." and
// "..", empty components, backslashes, control characters, over-long names.
Status parse_rel_path(const std::string& s, RelPath& out);
// Same rules for a single new file/folder name.
Status validate_name(const std::string& name);
// Maps an errno value to a Status.
Status errno_to_status(int err, const char* what);

struct Resolved {
    ScopedFd dir;           // descriptor of the parent directory (always valid on success)
    std::string leaf;       // final component; empty when the path is the volume root
    RelPath rel;
    bool exists = false;
    struct stat st{};       // valid when exists (lstat semantics: symlinks are reported, then refused)
    bool is_dir() const { return exists && S_ISDIR(st.st_mode); }
    bool is_file() const { return exists && S_ISREG(st.st_mode); }
    // Opens the leaf itself as a directory (O_NOFOLLOW). Fails if it is not a directory.
    Status open_leaf_dir(ScopedFd& out) const;
};

class PathAuthorizer {
public:
    // Resolves `rel` under the volume root. Fails with forbidden on symlinks
    // anywhere in the path, not_found when an intermediate component is
    // missing or (must_exist) when the leaf is missing.
    static Status resolve(const VolumeInfo& v, const std::string& rel, Resolved& out, bool must_exist);
    static Status resolve(const VolumeInfo& v, const RelPath& rel, Resolved& out, bool must_exist);
    static bool is_protected(const VolumeInfo& v, const RelPath& rel);
    // Reads: volume must be mounted and device-owned.
    static Status check_read(const VolumeInfo& v);
    // Writes: additionally policy-writable, not fs-ro, and the path not protected.
    static Status check_write(const VolumeInfo& v, const RelPath& rel);
};

} // namespace dm
