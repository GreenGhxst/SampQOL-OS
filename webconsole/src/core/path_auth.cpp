#include "path_auth.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace dm {

ScopedFd::~ScopedFd() { reset(); }
ScopedFd& ScopedFd::operator=(ScopedFd&& o) noexcept { if (this != &o) { reset(o.fd_); o.fd_ = -1; } return *this; }
void ScopedFd::reset(int fd) { if (fd_ >= 0) ::close(fd_); fd_ = fd; }

std::string RelPath::str() const {
    std::string s;
    for (std::size_t i = 0; i < parts.size(); ++i) { if (i) s.push_back('/'); s += parts[i]; }
    return s;
}
RelPath RelPath::parent() const { RelPath p = *this; if (!p.parts.empty()) p.parts.pop_back(); return p; }
RelPath RelPath::child(const std::string& name) const { RelPath c = *this; c.parts.push_back(name); return c; }
bool RelPath::starts_with(const RelPath& prefix) const {
    if (prefix.parts.size() > parts.size()) return false;
    for (std::size_t i = 0; i < prefix.parts.size(); ++i) if (prefix.parts[i] != parts[i]) return false;
    return true;
}

bool names_equal_ci(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
bool RelPath::starts_with_ci(const RelPath& prefix) const {
    if (prefix.parts.size() > parts.size()) return false;
    for (std::size_t i = 0; i < prefix.parts.size(); ++i) if (!names_equal_ci(prefix.parts[i], parts[i])) return false;
    return true;
}

Status validate_name(const std::string& name) {
    if (name.empty()) return Status::fail(Err::invalid_argument, "empty name");
    if (name.size() > 255) return Status::fail(Err::invalid_argument, "name too long");
    if (name == "." || name == "..") return Status::fail(Err::invalid_argument, "invalid name");
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f) return Status::fail(Err::invalid_argument, "control character in name");
        if (c == '/' || c == '\\') return Status::fail(Err::invalid_argument, "separator in name");
    }
    // exFAT/FAT reserved characters: refuse so names stay portable to the host.
    for (char c : std::string("<>:\"|?*")) if (name.find(c) != std::string::npos)
        return Status::fail(Err::invalid_argument, std::string("character not allowed in name: ") + c);
    if (name.back() == ' ' || name.back() == '.') return Status::fail(Err::invalid_argument, "name may not end with space or dot");
    return Status::good();
}

Status parse_rel_path(const std::string& s, RelPath& out) {
    out.parts.clear();
    if (s.size() > 4096) return Status::fail(Err::invalid_argument, "path too long");
    if (!s.empty() && s[0] == '/') return Status::fail(Err::invalid_argument, "absolute path not allowed");
    if (s.find('\\') != std::string::npos) return Status::fail(Err::invalid_argument, "backslash not allowed");
    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = s.find('/', i);
        if (j == std::string::npos) j = s.size();
        std::string part = s.substr(i, j - i);
        if (part.empty()) {
            if (j == s.size()) break;                       // trailing slash or empty path
            return Status::fail(Err::invalid_argument, "empty path component");
        }
        if (part == "." || part == "..") return Status::fail(Err::invalid_argument, "relative component not allowed");
        Status st = validate_name(part);
        if (!st.ok()) return st;
        out.parts.push_back(part);
        if (out.parts.size() > 64) return Status::fail(Err::invalid_argument, "path too deep");
        i = j + 1;
    }
    return Status::good();
}

namespace {
constexpr int kDirFlags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;

} // namespace

Status errno_to_status(int err, const char* what) {
    switch (err) {
        case ENOENT: return Status::fail(Err::not_found, std::string(what) + ": not found");
        case ELOOP:  return Status::fail(Err::forbidden, std::string(what) + ": symbolic links are not allowed");
        case ENOTDIR: return Status::fail(Err::not_found, std::string(what) + ": not a directory");
        case EACCES: case EPERM: return Status::fail(Err::forbidden, std::string(what) + ": permission denied");
        case ENOSPC: return Status::fail(Err::no_space, std::string(what) + ": no space left");
        case EEXIST: return Status::fail(Err::exists, std::string(what) + ": already exists");
        case EROFS: return Status::fail(Err::read_only, std::string(what) + ": read-only filesystem");
        case EBUSY: return Status::fail(Err::busy, std::string(what) + ": busy");
        case ENOTEMPTY: return Status::fail(Err::exists, std::string(what) + ": directory not empty");
        case EXDEV: return Status::fail(Err::unsupported, std::string(what) + ": cross-device");
        default: return Status::fail(Err::io, std::string(what) + ": " + std::strerror(err));
    }
}
namespace { inline Status errno_status(const char* what) { return errno_to_status(errno, what); } }

Status Resolved::open_leaf_dir(ScopedFd& out) const {
    if (leaf.empty()) {
        int fd = ::openat(dir.get(), ".", kDirFlags);
        if (fd < 0) return errno_status("open");
        out.reset(fd); return Status::good();
    }
    if (!exists) return Status::fail(Err::not_found, "not found");
    if (!S_ISDIR(st.st_mode)) return Status::fail(Err::invalid_argument, "not a directory");
    int fd = ::openat(dir.get(), leaf.c_str(), kDirFlags);
    if (fd < 0) return errno_status("open");
    out.reset(fd); return Status::good();
}

Status PathAuthorizer::resolve(const VolumeInfo& v, const std::string& rel, Resolved& out, bool must_exist) {
    RelPath rp;
    Status st = parse_rel_path(rel, rp);
    if (!st.ok()) return st;
    return resolve(v, rp, out, must_exist);
}

Status PathAuthorizer::resolve(const VolumeInfo& v, const RelPath& rel, Resolved& out, bool must_exist) {
    if (!v.mounted) return Status::fail(Err::unavailable, "volume is not mounted");
    // The root comes from policy, not the client: following links here is fine,
    // but the result must be a directory.
    int rfd = ::open(v.root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (rfd < 0) return Status::fail(Err::unavailable, "volume root unavailable: " + v.root);
    ScopedFd cur(rfd);
    out.rel = rel;
    for (std::size_t i = 0; i + 1 < rel.parts.size(); ++i) {
        struct stat cst{};
        if (::fstatat(cur.get(), rel.parts[i].c_str(), &cst, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(cst.st_mode))
            return Status::fail(Err::forbidden, "symbolic links are not allowed");
        int nfd = ::openat(cur.get(), rel.parts[i].c_str(), kDirFlags);   // O_NOFOLLOW guards the race
        if (nfd < 0) return errno == ELOOP || errno == ENOTDIR && [&] { struct stat s2{}; return ::fstatat(cur.get(), rel.parts[i].c_str(), &s2, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(s2.st_mode); }()
            ? Status::fail(Err::forbidden, "symbolic links are not allowed") : errno_status(rel.parts[i].c_str());
        cur.reset(nfd);
    }
    out.dir = std::move(cur);
    out.leaf = rel.leaf();
    out.exists = false;
    if (out.leaf.empty()) {
        if (::fstat(out.dir.get(), &out.st) != 0) return errno_status("stat");
        out.exists = true;
        return Status::good();
    }
    if (::fstatat(out.dir.get(), out.leaf.c_str(), &out.st, AT_SYMLINK_NOFOLLOW) == 0) {
        if (S_ISLNK(out.st.st_mode)) return Status::fail(Err::forbidden, "symbolic links are not allowed");
        out.exists = true;
    } else if (errno != ENOENT) {
        return errno_status("stat");
    }
    if (must_exist && !out.exists) return Status::fail(Err::not_found, rel.str() + ": not found");
    return Status::good();
}

bool PathAuthorizer::is_protected(const VolumeInfo& v, const RelPath& rel) {
    for (const auto& p : v.protected_paths) {
        RelPath pp;
        if (!parse_rel_path(p, pp).ok() || pp.empty()) continue;
        if (rel.starts_with_ci(pp)) return true;   // case-insensitive: exFAT/FAT would open "AUTORUN" as "autorun"
    }
    return false;
}

Status PathAuthorizer::check_read(const VolumeInfo& v) {
    if (!v.mounted) return Status::fail(Err::unavailable, "volume is not mounted");
    if (v.ownership == Ownership::usb_host) return Status::fail(Err::busy, "volume is exported to the USB host (SD Card Access); the device must own it");
    if (v.ownership != Ownership::device) return Status::fail(Err::unavailable, "volume ownership unknown");
    return Status::good();
}

Status PathAuthorizer::check_write(const VolumeInfo& v, const RelPath& rel) {
    Status st = check_read(v);
    if (!st.ok()) return st;
    if (!v.policy_writable) return Status::fail(Err::read_only, "volume is read-only by policy");
    if (v.fs_read_only) return Status::fail(Err::read_only, "filesystem is mounted read-only");
    if (rel.empty()) return Status::fail(Err::forbidden, "the volume root itself cannot be modified");
    if (is_protected(v, rel)) return Status::fail(Err::forbidden, "protected path: " + rel.str());
    return Status::good();
}

} // namespace dm
