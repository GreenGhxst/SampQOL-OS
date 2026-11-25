#include "file_ops.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "../util/sha256.h"

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

namespace dm {

namespace {
constexpr int kDirFlags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
constexpr std::size_t kCopyBuf = 1 << 20;
constexpr const char* kDataDir = ".diskmode";
constexpr const char* kTrashDir = "trash";
std::mutex g_trash_mutex;

int rename_noreplace(int olddir, const char* oldp, int newdir, const char* newp) {
#ifdef SYS_renameat2
    int r = (int)::syscall(SYS_renameat2, olddir, oldp, newdir, newp, (unsigned)RENAME_NOREPLACE);
    if (r == 0 || (errno != EINVAL && errno != ENOSYS)) return r;
#endif
    // Fallback (filesystem without flag support): check-then-rename. Not
    // atomic, so only used when the kernel refuses the flag.
    struct stat st{};
    if (::fstatat(newdir, newp, &st, AT_SYMLINK_NOFOLLOW) == 0) { errno = EEXIST; return -1; }
    return ::renameat(olddir, oldp, newdir, newp);
}

std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string ext_of(const std::string& n) { auto p = n.rfind('.'); return p == std::string::npos || p == 0 ? "" : lower(n.substr(p + 1)); }
} // namespace

std::string file_kind(const std::string& name) {
    static const char* audio[] = {"wav", "aif", "aiff", "flac", "mp3", "ogg", "m4a", "snd"};
    static const char* project[] = {"xpj", "xpm", "xpl", "sxq", "mid", "midi", "xal", "progression"};
    std::string e = ext_of(name);
    for (auto a : audio) if (e == a) return "audio";
    for (auto p : project) if (e == p) return "project";
    return "other";
}

json Entry::to_json() const {
    return json{{"name", name}, {"type", type}, {"size", size}, {"mtime", mtime},
                {"kind", type == "dir" ? "dir" : file_kind(name)}, {"protected", is_protected}};
}
json TrashItem::to_json() const {
    return json{{"id", id}, {"name", name}, {"original_path", original_path}, {"size", size}, {"deleted_at", deleted_at}};
}

Conflict conflict_from_string(const std::string& s) {
    if (s == "rename") return Conflict::rename;
    if (s == "overwrite") return Conflict::overwrite;
    return Conflict::fail;
}

FileOps::FileOps(Journal& journal, std::uint64_t margin) : journal_(journal), margin_(margin) {}

Status FileOps::errno_status(const char* what) { return errno_to_status(errno, what); }

std::string FileOps::unique_name(int dirfd, const std::string& name) {
    std::string stem = name, ext;
    auto dot = name.rfind('.');
    if (dot != std::string::npos && dot != 0) { stem = name.substr(0, dot); ext = name.substr(dot); }
    for (int i = 2; i < 10000; ++i) {
        std::string cand = stem + " " + std::to_string(i) + ext;
        struct stat st{};
        if (::fstatat(dirfd, cand.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT) return cand;
    }
    return stem + " " + random_token(4) + ext;
}

std::uint64_t FileOps::usable_free(const VolumeInfo& v) const {
    struct statvfs st{};
    if (::statvfs(v.root.c_str(), &st) != 0) return 0;
    std::uint64_t free = std::uint64_t(st.f_bavail) * st.f_frsize;
    return free > margin_ ? free - margin_ : 0;
}

// ---------------------------------------------------------------- listing

Status FileOps::list(const VolumeInfo& v, const RelPath& dir, const ListOptions& o, Listing& out) {
    Status st = PathAuthorizer::check_read(v);
    if (!st.ok()) return st;
    Resolved r;
    st = PathAuthorizer::resolve(v, dir, r, true);
    if (!st.ok()) return st;
    if (!r.is_dir()) return Status::fail(Err::invalid_argument, "not a directory");
    ScopedFd dfd;
    st = r.open_leaf_dir(dfd);
    if (!st.ok()) return st;
    int dupfd = ::dup(dfd.get());
    DIR* d = ::fdopendir(dupfd);
    if (!d) { ::close(dupfd); return errno_status("opendir"); }
    std::vector<Entry> all;
    std::string filter = lower(o.filter);
    const std::size_t kMaxEntries = 20000;
    while (dirent* e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        if (!o.show_hidden && name[0] == '.') continue;
        if (names_equal_ci(name, kDataDir)) continue;         // application data, never listed (case-insensitive fs)
        if (!filter.empty() && lower(name).find(filter) == std::string::npos) continue;
        struct stat s{};
        if (::fstatat(dfd.get(), name.c_str(), &s, AT_SYMLINK_NOFOLLOW) != 0) continue;
        Entry en; en.name = name;
        en.type = S_ISDIR(s.st_mode) ? "dir" : S_ISREG(s.st_mode) ? "file" : "other";
        if (en.type == "other") continue;                      // symlinks, devices: hidden by design
        en.size = S_ISREG(s.st_mode) ? std::uint64_t(s.st_size) : 0;
        en.mtime = s.st_mtime;
        en.is_protected = PathAuthorizer::is_protected(v, dir.child(name));
        if (!o.kind.empty()) {
            if (o.kind == "dir" && en.type != "dir") continue;
            if (o.kind == "file" && en.type != "file") continue;
            if ((o.kind == "audio" || o.kind == "project" || o.kind == "other") && (en.type != "file" || file_kind(name) != o.kind)) continue;
        }
        all.push_back(std::move(en));
        if (all.size() >= kMaxEntries) { out.truncated = true; break; }
    }
    ::closedir(d);
    auto cmp = [&](const Entry& a, const Entry& b) {
        if (a.type != b.type) return a.type == "dir";          // folders first
        int c = 0;
        if (o.sort == "size") c = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
        else if (o.sort == "mtime") c = a.mtime < b.mtime ? -1 : a.mtime > b.mtime ? 1 : 0;
        else if (o.sort == "type") c = ext_of(a.name).compare(ext_of(b.name));
        if (c == 0) c = lower(a.name).compare(lower(b.name));
        return o.descending ? c > 0 : c < 0;
    };
    std::sort(all.begin(), all.end(), cmp);
    out.total = all.size();
    std::size_t limit = std::min<std::size_t>(o.limit, 2000);
    if (o.offset < all.size())
        out.entries.assign(all.begin() + o.offset, all.begin() + std::min(all.size(), o.offset + limit));
    return Status::good();
}

Status FileOps::stat(const VolumeInfo& v, const RelPath& p, Entry& out) {
    Status st = PathAuthorizer::check_read(v);
    if (!st.ok()) return st;
    Resolved r;
    st = PathAuthorizer::resolve(v, p, r, true);
    if (!st.ok()) return st;
    out.name = p.empty() ? v.name : p.leaf();
    out.type = r.is_dir() ? "dir" : r.is_file() ? "file" : "other";
    out.size = r.is_file() ? std::uint64_t(r.st.st_size) : 0;
    out.mtime = r.st.st_mtime;
    out.is_protected = PathAuthorizer::is_protected(v, p);
    return Status::good();
}

// ---------------------------------------------------------------- mkdir / rename

Status FileOps::mkdir(const VolumeInfo& v, const RelPath& p) {
    Status st = PathAuthorizer::check_write(v, p);
    if (!st.ok()) return st;
    Resolved r;
    st = PathAuthorizer::resolve(v, p, r, false);
    if (!st.ok()) return st;
    if (r.exists) return Status::fail(Err::exists, "a file or folder with this name already exists");
    std::string jid = journal_.begin("mkdir", v.id + ":" + p.str(), "");
    if (::mkdirat(r.dir.get(), r.leaf.c_str(), 0755) != 0) { st = errno_status("mkdir"); journal_.finish(jid, "failed", st.message); return st; }
    journal_.finish(jid, "done");
    return Status::good();
}

Status FileOps::rename(const VolumeInfo& v, const RelPath& p, const std::string& new_name) {
    Status st = validate_name(new_name);
    if (!st.ok()) return st;
    st = PathAuthorizer::check_write(v, p);
    if (!st.ok()) return st;
    st = PathAuthorizer::check_write(v, p.parent().child(new_name));
    if (!st.ok()) return st;
    Resolved r;
    st = PathAuthorizer::resolve(v, p, r, true);
    if (!st.ok()) return st;
    if (new_name == r.leaf) return Status::good();
    std::string jid = journal_.begin("rename", v.id + ":" + p.str(), v.id + ":" + p.parent().child(new_name).str());
    if (rename_noreplace(r.dir.get(), r.leaf.c_str(), r.dir.get(), new_name.c_str()) != 0) {
        st = errno_status("rename"); journal_.finish(jid, "failed", st.message); return st;
    }
    journal_.finish(jid, "done");
    return Status::good();
}

// ---------------------------------------------------------------- copy / move

Status FileOps::pick_destination(int ddir, const std::string& dabs, const std::string& wanted, Conflict c,
                                 std::string& final_name, bool& overwrite) {
    (void)dabs;
    overwrite = false;
    struct stat st{};
    if (::fstatat(ddir, wanted.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno != ENOENT) return errno_status("stat destination");
        final_name = wanted; return Status::good();
    }
    switch (c) {
        case Conflict::fail: return Status::fail(Err::exists, "destination already exists: " + wanted);
        case Conflict::rename: final_name = unique_name(ddir, wanted); return Status::good();
        case Conflict::overwrite:
            if (S_ISDIR(st.st_mode)) { final_name = wanted; overwrite = true; return Status::good(); } // merge
            if (!S_ISREG(st.st_mode)) return Status::fail(Err::forbidden, "destination is not a regular file");
            final_name = wanted; overwrite = true; return Status::good();
    }
    return Status::fail(Err::invalid_argument, "bad conflict policy");
}

Status FileOps::tree_size(int dir, const std::string& name, std::uint64_t& bytes, std::uint64_t& count, std::size_t limit) {
    struct stat st{};
    if (::fstatat(dir, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) return errno_status("stat");
    if (++count > limit) return Status::fail(Err::unsupported, "too many items");
    if (S_ISREG(st.st_mode)) { bytes += std::uint64_t(st.st_size); return Status::good(); }
    if (!S_ISDIR(st.st_mode)) return Status::good();
    int fd = ::openat(dir, name.c_str(), kDirFlags);
    if (fd < 0) return errno_status("open");
    DIR* d = ::fdopendir(fd);
    if (!d) { ::close(fd); return errno_status("opendir"); }
    Status st2 = Status::good();
    while (dirent* e = ::readdir(d)) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        st2 = tree_size(fd, e->d_name, bytes, count, limit);
        if (!st2.ok()) break;
    }
    ::closedir(d);
    return st2;
}

Status FileOps::copy_regular(int sdir, const std::string& sname, int ddir, const std::string& dname,
                             const OpContext& ctx, std::uint64_t& done, std::uint64_t total, const std::string& dabs,
                             std::vector<std::string>& temps, bool overwrite) {
    int sfd = ::openat(sdir, sname.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (sfd < 0) return errno_status(sname.c_str());
    ScopedFd src(sfd);
    struct stat sst{};
    if (::fstat(sfd, &sst) != 0 || !S_ISREG(sst.st_mode)) return Status::fail(Err::invalid_argument, "source is not a regular file");
    std::string tmp = std::string(".dmtmp-") + random_token(6);
    int tfd = ::openat(ddir, tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (tfd < 0) return errno_status("create temporary file");
    ScopedFd dst(tfd);
    temps.push_back(dabs + "/" + tmp);
    auto cleanup = [&]() { ::unlinkat(ddir, tmp.c_str(), 0); };
    std::vector<char> buf(kCopyBuf);
    // Sample ownership at file start and approximately each MiB, then always
    // recheck at irreversible commit. Avoid a full mount/configfs scan per IO block.
    std::uint64_t next_check = done;
    for (;;) {
        if (done >= next_check) {
            if (ctx.cancelled()) { cleanup(); return Status::fail(Err::cancelled, "cancelled"); }
            next_check = done + 1024 * 1024;
        }
        ssize_t n = ::read(sfd, buf.data(), buf.size());
        if (n < 0) { if (errno == EINTR) continue; Status s = errno_status("read"); cleanup(); return s; }
        if (n == 0) break;
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = ::write(tfd, buf.data() + off, std::size_t(n - off));
            if (w < 0) { if (errno == EINTR) continue; Status s = errno_status("write"); cleanup(); return s; }
            off += w;
        }
        done += std::uint64_t(n);
        if (ctx.progress) ctx.progress(done, total);
    }
    if (ctx.cancelled()) { cleanup(); return Status::fail(Err::cancelled, "cancelled before commit"); }
    if (::fsync(tfd) != 0) { Status s = errno_status("fsync"); cleanup(); return s; }
    if (ctx.cancelled()) { cleanup(); return Status::fail(Err::cancelled, "cancelled before commit"); }
    struct timespec times[2] = {sst.st_atim, sst.st_mtim};
    ::futimens(tfd, times);
    dst.reset();
    if (ctx.cancelled()) { cleanup(); return Status::fail(Err::cancelled, "cancelled before commit"); }
    int rc = overwrite ? ::renameat(ddir, tmp.c_str(), ddir, dname.c_str())
                       : rename_noreplace(ddir, tmp.c_str(), ddir, dname.c_str());
    if (rc != 0) { Status s = errno_status("commit"); cleanup(); return s; }
    temps.pop_back();
    return Status::good();
}

Status FileOps::copy_tree(int sdir, const std::string& sname, int ddir, const std::string& dname,
                          const OpContext& ctx, std::uint64_t& done, std::uint64_t total, std::vector<std::string>& temps,
                          const std::string& dabs, bool overwrite) {
    struct stat st{};
    if (::fstatat(sdir, sname.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) return errno_status(sname.c_str());
    if (S_ISREG(st.st_mode)) {
        if (overwrite) {   // never replace a directory with a file, even when merging
            struct stat dst{};
            if (::fstatat(ddir, dname.c_str(), &dst, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(dst.st_mode))
                return Status::fail(Err::exists, "a folder with this name exists: " + dname);
        }
        return copy_regular(sdir, sname, ddir, dname, ctx, done, total, dabs, temps, overwrite);
    }
    if (!S_ISDIR(st.st_mode)) return Status::fail(Err::forbidden, "skipped special file: " + sname);
    if (ctx.cancelled()) return Status::fail(Err::cancelled, "cancelled before directory creation");
    if (::mkdirat(ddir, dname.c_str(), 0755) != 0 && errno != EEXIST) return errno_status("mkdir");
    int sfd = ::openat(sdir, sname.c_str(), kDirFlags);
    if (sfd < 0) return errno_status(sname.c_str());
    ScopedFd s(sfd);
    int dfd = ::openat(ddir, dname.c_str(), kDirFlags);
    if (dfd < 0) return errno_status(dname.c_str());
    ScopedFd d(dfd);
    DIR* dir = ::fdopendir(::dup(sfd));
    if (!dir) return errno_status("opendir");
    std::size_t failures = 0; std::string first;
    while (dirent* e = ::readdir(dir)) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        if (ctx.cancelled()) {
            ::closedir(dir);
            return Status::fail(Err::partial, "Copy interrupted; destination directory may be incomplete");
        }
        Status r = copy_tree(sfd, e->d_name, dfd, e->d_name, ctx, done, total, temps, dabs + "/" + dname, overwrite);
        if (r.code == Err::cancelled || r.code == Err::partial) {
            ::closedir(dir);
            return Status::fail(Err::partial, "Copy interrupted or incomplete; destination directory may contain partial results: " + r.message);
        }
        if (!r.ok()) { if (!failures++) first = r.message; }
    }
    ::closedir(dir);
    if (ctx.cancelled()) return Status::fail(Err::partial, "Copy interrupted; destination directory may be incomplete");
    if (failures) return Status::fail(Err::partial, std::to_string(failures) + " item(s) failed; first: " + first);
    return Status::good();
}

Status FileOps::copy(const VolumeInfo& sv, const RelPath& src, const VolumeInfo& dv, const RelPath& dst_dir,
                     const std::string& name, Conflict c, const OpContext& ctx, std::string* final_name) {
    if (src.empty()) return Status::fail(Err::invalid_argument, "cannot copy a volume root");
    Status st = PathAuthorizer::check_read(sv);
    if (!st.ok()) return st;
    std::string wanted = name.empty() ? src.leaf() : name;
    st = validate_name(wanted);
    if (!st.ok()) return st;
    st = PathAuthorizer::check_write(dv, dst_dir.child(wanted));
    if (!st.ok()) return st;
    if (sv.id == dv.id && (dst_dir.starts_with(src))) return Status::fail(Err::invalid_argument, "cannot copy a folder into itself");
    Resolved rs, rd;
    st = PathAuthorizer::resolve(sv, src, rs, true); if (!st.ok()) return st;
    st = PathAuthorizer::resolve(dv, dst_dir, rd, true); if (!st.ok()) return st;
    if (!rd.is_dir()) return Status::fail(Err::invalid_argument, "destination is not a folder");
    ScopedFd ddir; st = rd.open_leaf_dir(ddir); if (!st.ok()) return st;
    std::uint64_t total = 0, count = 0;
    st = tree_size(rs.dir.get(), rs.leaf, total, count, 200000); if (!st.ok()) return st;
    if (total > usable_free(dv)) return Status::fail(Err::no_space, "not enough free space on " + dv.name + " (need " + human_bytes(total) + ")");
    std::string dabs = dv.root + (dst_dir.empty() ? "" : "/" + dst_dir.str());
    std::string fname; bool overwrite = false;
    st = pick_destination(ddir.get(), dabs, wanted, c, fname, overwrite); if (!st.ok()) return st;
    std::vector<std::string> temps;
    std::string jid = journal_.begin("copy", sv.id + ":" + src.str(), dv.id + ":" + dst_dir.child(fname).str());
    std::uint64_t done = 0;
    if (rs.is_file()) st = copy_regular(rs.dir.get(), rs.leaf, ddir.get(), fname, ctx, done, total, dabs, temps, overwrite);
    else st = copy_tree(rs.dir.get(), rs.leaf, ddir.get(), fname, ctx, done, total, temps, dabs, overwrite);
    for (auto& t : temps) ::unlink(t.c_str());
    journal_.finish(jid, st.ok() ? "done" : st.code == Err::cancelled ? "cancelled" : st.code == Err::partial ? "partial" : "failed", st.message);
    if (final_name) *final_name = fname;
    return st;
}

Status FileOps::move(const VolumeInfo& sv, const RelPath& src, const VolumeInfo& dv, const RelPath& dst_dir,
                     const std::string& name, Conflict c, const OpContext& ctx, std::string* final_name) {
    if (src.empty()) return Status::fail(Err::invalid_argument, "cannot move a volume root");
    Status st = PathAuthorizer::check_write(sv, src);
    if (!st.ok()) return st;
    std::string wanted = name.empty() ? src.leaf() : name;
    st = validate_name(wanted); if (!st.ok()) return st;
    st = PathAuthorizer::check_write(dv, dst_dir.child(wanted)); if (!st.ok()) return st;
    if (sv.id == dv.id && dst_dir.starts_with(src)) return Status::fail(Err::invalid_argument, "cannot move a folder into itself");
    if (sv.id == dv.id && dst_dir.str() == src.parent().str() && wanted == src.leaf()) return Status::good();
    if (ctx.cancelled()) return Status::fail(Err::cancelled, "cancelled before move");
    if (sv.id == dv.id) {
        Resolved rs, rd;
        st = PathAuthorizer::resolve(sv, src, rs, true); if (!st.ok()) return st;
        st = PathAuthorizer::resolve(dv, dst_dir, rd, true); if (!st.ok()) return st;
        if (!rd.is_dir()) return Status::fail(Err::invalid_argument, "destination is not a folder");
        ScopedFd ddir; st = rd.open_leaf_dir(ddir); if (!st.ok()) return st;
        std::string fname; bool overwrite = false;
        st = pick_destination(ddir.get(), "", wanted, c, fname, overwrite); if (!st.ok()) return st;
        if (overwrite && rs.is_dir()) return Status::fail(Err::exists, "cannot overwrite a folder by moving; copy and merge instead");
        std::string jid = journal_.begin("move", sv.id + ":" + src.str(), dv.id + ":" + dst_dir.child(fname).str());
        if (ctx.cancelled()) { journal_.finish(jid,"cancelled","Storage ownership changed"); return Status::fail(Err::cancelled,"cancelled before move"); }
        int rc = overwrite ? ::renameat(rs.dir.get(), rs.leaf.c_str(), ddir.get(), fname.c_str())
                           : rename_noreplace(rs.dir.get(), rs.leaf.c_str(), ddir.get(), fname.c_str());
        if (rc != 0 && errno != EXDEV) { st = errno_status("move"); journal_.finish(jid, "failed", st.message); return st; }
        if (rc == 0) { journal_.finish(jid, "done"); if (final_name) *final_name = fname; return Status::good(); }
        journal_.finish(jid, "failed", "cross-device inside one volume; falling back to copy");
    }
    // Cross-volume: verified copy, then delete the source only on full success.
    std::string fname;
    st = copy(sv, src, dv, dst_dir, wanted, c, ctx, &fname);
    if (!st.ok()) return st;
    // Destination is committed: never delete the source after revocation.
    if (ctx.cancelled())
        return Status::fail(Err::partial,"Copied, but source retained after cancellation or storage revocation");
    Resolved rs;
    Status s2 = PathAuthorizer::resolve(sv, src, rs, true);
    if (!s2.ok()) return s2;
    if (ctx.cancelled())
        return Status::fail(Err::partial,"Copied, but source retained after cancellation or storage revocation");
    std::string jid = journal_.begin("move-delete-source", sv.id + ":" + src.str(), dv.id + ":" + dst_dir.child(fname).str());
    s2 = remove_tree(rs.dir.get(), rs.leaf, &ctx);
    journal_.finish(jid, s2.ok() ? "done" : "partial", s2.message);
    if (final_name) *final_name = fname;
    if (!s2.ok()) return Status::fail(Err::partial, "copied, but the source could not be removed: " + s2.message);
    return Status::good();
}

// ---------------------------------------------------------------- delete / trash

Status FileOps::remove_tree(int dir, const std::string& name, const OpContext* ctx) {
    if (ctx && ctx->cancelled()) return Status::fail(Err::cancelled, "Source deletion interrupted by cancellation or storage revocation");
    struct stat st{};
    if (::fstatat(dir, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) return errno_status(name.c_str());
    if (!S_ISDIR(st.st_mode)) {
        if (ctx && ctx->cancelled()) return Status::fail(Err::cancelled, "Source deletion interrupted");
        if (::unlinkat(dir, name.c_str(), 0) != 0) return errno_status(name.c_str());
        return Status::good();
    }
    int fd = ::openat(dir, name.c_str(), kDirFlags);
    if (fd < 0) return errno_status(name.c_str());
    DIR* d = ::fdopendir(fd);
    if (!d) { ::close(fd); return errno_status("opendir"); }
    Status first = Status::good();
    while (dirent* e = ::readdir(d)) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        Status r = remove_tree(fd, e->d_name, ctx);
        if (!r.ok()) { if (first.ok()) first = r; if (ctx) break; }
    }
    ::closedir(d);
    if (!first.ok()) return first;
    if (ctx && ctx->cancelled()) return Status::fail(Err::cancelled, "Source deletion interrupted");
    if (::unlinkat(dir, name.c_str(), AT_REMOVEDIR) != 0) return errno_status(name.c_str());
    return Status::good();
}

Status FileOps::open_trash(const VolumeInfo& v, ScopedFd& trash, bool create) {
    int rfd = ::open(v.root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (rfd < 0) return Status::fail(Err::unavailable, "volume root unavailable");
    ScopedFd root(rfd);
    if (create) ::mkdirat(rfd, kDataDir, 0755);
    int dfd = ::openat(rfd, kDataDir, kDirFlags);
    if (dfd < 0) return Status::fail(Err::unsupported, "trash is not available on this volume");
    ScopedFd data(dfd);
    if (create) ::mkdirat(dfd, kTrashDir, 0755);
    int tfd = ::openat(dfd, kTrashDir, kDirFlags);
    if (tfd < 0) return Status::fail(Err::unsupported, "trash is not available on this volume");
    trash.reset(tfd);
    return Status::good();
}

Status FileOps::read_trash_index(int trashfd, json& idx) {
    idx = json::array();
    int fd = ::openat(trashfd, "index.json", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT ? Status::good() : errno_status("trash index");
    std::string s; char buf[4096]; ssize_t n;
    while ((n = ::read(fd, buf, sizeof buf)) > 0) s.append(buf, std::size_t(n));
    const int saved_errno = errno;
    ::close(fd);
    if (n < 0) return errno_to_status(saved_errno, "read trash index");
    json j = json::parse(s, nullptr, false);
    if (j.is_discarded() || !j.is_array())
        return Status::fail(Err::io, "trash recovery index is corrupt");
    idx = std::move(j);
    return Status::good();
}

Status FileOps::write_trash_index(int trashfd, const json& idx) {
    std::string tmp = "index.json.tmp";
    int fd = ::openat(trashfd, tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return errno_status("trash index");
    std::string s = idx.dump();
    bool ok = ::write(fd, s.data(), s.size()) == (ssize_t)s.size() && ::fsync(fd) == 0;
    ::close(fd);
    if (!ok || ::renameat(trashfd, tmp.c_str(), trashfd, "index.json") != 0) return errno_status("trash index");
    return Status::good();
}

Status FileOps::remove(const VolumeInfo& v, const RelPath& p, bool permanent, std::string* trash_id) {
    if (p.empty()) return Status::fail(Err::invalid_argument, "cannot delete a volume root");
    Status st = PathAuthorizer::check_write(v, p);
    if (!st.ok()) return st;
    Resolved r;
    st = PathAuthorizer::resolve(v, p, r, true); if (!st.ok()) return st;
    if (permanent) {
        std::string jid = journal_.begin("delete", v.id + ":" + p.str(), "");
        st = remove_tree(r.dir.get(), r.leaf);
        journal_.finish(jid, st.ok() ? "done" : "failed", st.message);
        return st;
    }
    std::lock_guard<std::mutex> lk(g_trash_mutex);
    ScopedFd trash;
    st = open_trash(v, trash, true); if (!st.ok()) return st;
    std::string id = std::to_string(now_ms()) + "-" + random_token(4);
    if (::mkdirat(trash.get(), id.c_str(), 0755) != 0) return errno_status("trash");
    int idfd = ::openat(trash.get(), id.c_str(), kDirFlags);
    if (idfd < 0) return errno_status("trash");
    ScopedFd slot(idfd);
    std::uint64_t bytes = 0, count = 0;
    tree_size(r.dir.get(), r.leaf, bytes, count, 1000000);
    std::string jid = journal_.begin("trash", v.id + ":" + p.str(), v.id + ":" + std::string(kDataDir) + "/trash/" + id);
    if (::renameat(r.dir.get(), r.leaf.c_str(), idfd, r.leaf.c_str()) != 0) {
        st = errno_status("move to trash");
        ::unlinkat(trash.get(), id.c_str(), AT_REMOVEDIR);
        journal_.finish(jid, "failed", st.message);
        if (st.code == Err::unsupported) return Status::fail(Err::unsupported, "trash is not possible across devices; use a permanent delete");
        return st;
    }
    json idx;
    st = read_trash_index(trash.get(), idx);
    if (st.ok()) {
        idx.push_back(json{{"id", id}, {"name", r.leaf}, {"original_path", p.str()}, {"size", bytes}, {"deleted_at", iso8601_now()}});
        st = write_trash_index(trash.get(), idx);
    }
    if (trash_id) *trash_id = id;
    if (!st.ok()) {
        // The source already moved. Preserve the slot ID in the journal and
        // return a PARTIAL result, never success or a retry-safe failure.
        const std::string error = "File moved to trash slot " + id +
            " but its recovery index could not be saved: " + st.message;
        journal_.finish(jid, "partial", error);
        return Status::fail(Err::partial, error);
    }
    journal_.finish(jid, "done");
    return Status::good();
}

Status FileOps::list_trash(const VolumeInfo& v, std::vector<TrashItem>& out) {
    Status st = PathAuthorizer::check_read(v); if (!st.ok()) return st;
    std::lock_guard<std::mutex> lk(g_trash_mutex);
    ScopedFd trash;
    st = open_trash(v, trash, false);
    if (!st.ok()) return st.code == Err::unsupported ? Status::good() : st;
    json idx; st = read_trash_index(trash.get(), idx); if (!st.ok()) return st;
    for (const auto& e : idx) {
        TrashItem t; t.id = e.value("id", ""); t.name = e.value("name", ""); t.original_path = e.value("original_path", "");
        t.size = e.value("size", 0ull); t.deleted_at = e.value("deleted_at", "");
        struct stat s{};
        if (::fstatat(trash.get(), t.id.c_str(), &s, AT_SYMLINK_NOFOLLOW) != 0) continue;
        out.push_back(t);
    }
    return Status::good();
}

Status FileOps::restore(const VolumeInfo& v, const std::string& trash_id, std::string* restored_path) {
    if (trash_id.empty() || trash_id.find('/') != std::string::npos || trash_id[0] == '.') return Status::fail(Err::invalid_argument, "bad trash id");
    std::lock_guard<std::mutex> lk(g_trash_mutex);
    ScopedFd trash;
    Status st = open_trash(v, trash, false); if (!st.ok()) return st;
    json idx; st = read_trash_index(trash.get(), idx); if (!st.ok()) return st;
    json entry; std::size_t pos = 0;
    for (; pos < idx.size(); ++pos) if (idx[pos].value("id", "") == trash_id) { entry = idx[pos]; break; }
    if (entry.is_null()) return Status::fail(Err::not_found, "trash item not found");
    RelPath orig;
    st = parse_rel_path(entry.value("original_path", ""), orig); if (!st.ok()) return st;
    st = PathAuthorizer::check_write(v, orig); if (!st.ok()) return st;
    Resolved r;
    st = PathAuthorizer::resolve(v, orig, r, false);
    if (!st.ok()) return st.code == Err::not_found ? Status::fail(Err::not_found, "the original folder no longer exists") : st;
    if (r.exists) return Status::fail(Err::exists, "a file with the original name exists again; rename it first");
    int idfd = ::openat(trash.get(), trash_id.c_str(), kDirFlags);
    if (idfd < 0) return errno_status("trash item");
    ScopedFd slot(idfd);
    std::string name = entry.value("name", "");
    std::string jid = journal_.begin("restore", v.id + ":trash/" + trash_id, v.id + ":" + orig.str());
    if (rename_noreplace(idfd, name.c_str(), r.dir.get(), r.leaf.c_str()) != 0) { st = errno_status("restore"); journal_.finish(jid, "failed", st.message); return st; }
    slot.reset();
    ::unlinkat(trash.get(), trash_id.c_str(), AT_REMOVEDIR);
    idx.erase(pos);
    st = write_trash_index(trash.get(), idx);
    if (restored_path) *restored_path = orig.str();
    if (!st.ok()) {
        const std::string message="File restored, but trash index update failed: "+st.message;
        journal_.finish(jid,"partial",message);
        return Status::fail(Err::partial,message);
    }
    journal_.finish(jid, "done");
    return Status::good();
}

Status FileOps::purge_trash_item(const VolumeInfo& v, const std::string& trash_id) {
    if (trash_id.empty() || trash_id.find('/') != std::string::npos || trash_id[0] == '.') return Status::fail(Err::invalid_argument, "bad trash id");
    Status st = PathAuthorizer::check_read(v); if (!st.ok()) return st;
    if (!v.writable()) return Status::fail(Err::read_only, "volume is not writable");
    std::lock_guard<std::mutex> lk(g_trash_mutex);
    ScopedFd trash;
    st = open_trash(v, trash, false); if (!st.ok()) return st;
    json idx;
    st = read_trash_index(trash.get(), idx);
    if (!st.ok()) return st;
    std::string jid = journal_.begin("purge", v.id + ":trash/" + trash_id, "");
    st = remove_tree(trash.get(), trash_id);
    if (!st.ok()) {
        journal_.finish(jid,"failed",st.message);
        return st;
    }
    for (std::size_t i = 0; i < idx.size(); ++i) if (idx[i].value("id", "") == trash_id) { idx.erase(i); break; }
    st = write_trash_index(trash.get(), idx);
    if (!st.ok()) {
        const std::string message="Trash item purged, but index update failed: "+st.message;
        journal_.finish(jid,"partial",message);
        return Status::fail(Err::partial,message);
    }
    journal_.finish(jid,"done");
    return Status::good();
}

// ---------------------------------------------------------------- reads

Status FileOps::open_read(const VolumeInfo& v, const RelPath& p, ScopedFd& fd, std::uint64_t& size) {
    Status st = PathAuthorizer::check_read(v); if (!st.ok()) return st;
    Resolved r;
    st = PathAuthorizer::resolve(v, p, r, true); if (!st.ok()) return st;
    if (!r.is_file()) return Status::fail(Err::invalid_argument, "not a file");
    int f = ::openat(r.dir.get(), r.leaf.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (f < 0) return errno_status("open");
    fd.reset(f);
    size = std::uint64_t(r.st.st_size);
    return Status::good();
}

Status FileOps::sha256(const VolumeInfo& v, const RelPath& p, std::string& hex, const OpContext& ctx) {
    ScopedFd fd; std::uint64_t size = 0;
    Status st = open_read(v, p, fd, size); if (!st.ok()) return st;
    Sha256 h; std::vector<char> buf(kCopyBuf); std::uint64_t done = 0;
    for (;;) {
        if (ctx.cancelled()) return Status::fail(Err::cancelled, "cancelled");
        ssize_t n = ::read(fd.get(), buf.data(), buf.size());
        if (n < 0) { if (errno == EINTR) continue; return errno_status("read"); }
        if (n == 0) break;
        h.update(buf.data(), std::size_t(n));
        done += std::uint64_t(n);
        if (ctx.progress) ctx.progress(done, size);
    }
    hex = h.hex();
    return Status::good();
}

Status FileOps::place_file(const std::string& staged_abs, const VolumeInfo& dv, const RelPath& dst_dir,
                           const std::string& name, Conflict c, const OpContext& ctx, std::string* final_name) {
    Status st = validate_name(name); if (!st.ok()) return st;
    st = PathAuthorizer::check_write(dv, dst_dir.child(name)); if (!st.ok()) return st;
    Resolved rd;
    st = PathAuthorizer::resolve(dv, dst_dir, rd, true); if (!st.ok()) return st;
    if (!rd.is_dir()) return Status::fail(Err::invalid_argument, "destination is not a folder");
    ScopedFd ddir; st = rd.open_leaf_dir(ddir); if (!st.ok()) return st;
    struct stat sst{};
    if (::lstat(staged_abs.c_str(), &sst) != 0 || !S_ISREG(sst.st_mode)) return Status::fail(Err::not_found, "staged file missing");
    std::string fname; bool overwrite = false;
    st = pick_destination(ddir.get(), "", name, c, fname, overwrite); if (!st.ok()) return st;
    if (overwrite) { struct stat d{}; if (::fstatat(ddir.get(), fname.c_str(), &d, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(d.st_mode)) return Status::fail(Err::exists, "a folder with this name exists"); }
    std::string jid = journal_.begin("place", staged_abs, dv.id + ":" + dst_dir.child(fname).str());
    int rc = overwrite ? ::renameat(AT_FDCWD, staged_abs.c_str(), ddir.get(), fname.c_str())
                       : rename_noreplace(AT_FDCWD, staged_abs.c_str(), ddir.get(), fname.c_str());
    if (rc == 0) { journal_.finish(jid, "done"); if (final_name) *final_name = fname; return Status::good(); }
    if (errno != EXDEV) { st = errno_status("place"); journal_.finish(jid, "failed", st.message); return st; }
    if (std::uint64_t(sst.st_size) > usable_free(dv)) { journal_.finish(jid, "failed", "no space"); return Status::fail(Err::no_space, "not enough free space on " + dv.name); }
    std::vector<std::string> temps; std::uint64_t done = 0;
    std::string dabs = dv.root + (dst_dir.empty() ? "" : "/" + dst_dir.str());
    st = copy_regular(AT_FDCWD, staged_abs, ddir.get(), fname, ctx, done, std::uint64_t(sst.st_size), dabs, temps, overwrite);
    for (auto& t : temps) ::unlink(t.c_str());
    if (st.ok()) ::unlink(staged_abs.c_str());
    journal_.finish(jid, st.ok() ? "done" : "failed", st.message);
    if (final_name) *final_name = fname;
    return st;
}

Status FileOps::usage(const VolumeInfo& v, const RelPath& dir, std::uint64_t& files, std::uint64_t& dirs,
                      std::uint64_t& bytes, std::size_t max_entries) {
    Status st = PathAuthorizer::check_read(v); if (!st.ok()) return st;
    Resolved r;
    st = PathAuthorizer::resolve(v, dir, r, true); if (!st.ok()) return st;
    ScopedFd d; st = r.open_leaf_dir(d); if (!st.ok()) return st;
    files = dirs = bytes = 0;
    std::function<void(int, std::size_t)> walk = [&](int fd, std::size_t depth) {
        if (depth > 32 || files + dirs > max_entries) return;
        DIR* dd = ::fdopendir(::dup(fd));
        if (!dd) return;
        while (dirent* e = ::readdir(dd)) {
            if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
            if (depth == 0 && names_equal_ci(e->d_name, kDataDir)) continue;
            struct stat s{};
            if (::fstatat(fd, e->d_name, &s, AT_SYMLINK_NOFOLLOW) != 0) continue;
            if (S_ISREG(s.st_mode)) { ++files; bytes += std::uint64_t(s.st_size); }
            else if (S_ISDIR(s.st_mode)) {
                ++dirs;
                int c = ::openat(fd, e->d_name, kDirFlags);
                if (c >= 0) { walk(c, depth + 1); ::close(c); }
            }
            if (files + dirs > max_entries) break;
        }
        ::closedir(dd);
    };
    walk(d.get(), 0);
    return Status::good();
}

} // namespace dm
