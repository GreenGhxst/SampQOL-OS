#include "transfer_manager.h"
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace dm {

json Upload::to_json() const {
    return json{{"id", id}, {"volume", volume}, {"dir", dir}, {"name", name}, {"size", size}, {"received", received},
                {"state", state}, {"error", error}, {"inbox", inbox}, {"created", created}, {"updated", updated},
                {"sha256", final_sha256}};
}

TransferManager::TransferManager(std::string staging_dir, std::uint64_t max_upload_bytes, std::size_t max_open)
    : staging_dir_(std::move(staging_dir)), max_bytes_(max_upload_bytes), max_open_(max_open) {
    ::mkdir(staging_dir_.c_str(), 0700);
}

Status TransferManager::begin(const std::string& owner, const std::string& volume, const std::string& dir, const std::string& name,
                              std::uint64_t size, const std::string& expected_sha256, bool inbox, std::string& id_out) {
    Status st = validate_name(name); if (!st.ok()) return st;
    if (size > max_bytes_) return Status::fail(Err::invalid_argument, "file larger than the upload limit (" + human_bytes(max_bytes_) + ")");
    if (!expected_sha256.empty() && (expected_sha256.size() != 64 || expected_sha256.find_first_not_of("0123456789abcdef") != std::string::npos))
        return Status::fail(Err::invalid_argument, "sha256 must be 64 lowercase hex characters");
    struct statvfs sv{};
    if (::statvfs(staging_dir_.c_str(), &sv) == 0) {
        std::uint64_t free = std::uint64_t(sv.f_bavail) * sv.f_frsize;
        if (size + (16ull << 20) > free) return Status::fail(Err::no_space, "not enough space in the staging area");
    }
    std::lock_guard<std::mutex> lk(mu_);
    std::size_t open = 0;
    for (auto& [k, u] : uploads_) if (u->state == "open" && u->owner == owner) ++open;
    if (open >= max_open_) return Status::fail(Err::busy, "too many uploads in progress");
    auto u = std::make_unique<Upload>();
    u->id = random_token(16); u->owner = owner; u->volume = volume; u->dir = dir; u->name = name; u->size = size;
    u->expected_sha256 = expected_sha256; u->inbox = inbox; u->created = u->updated = now_ms();
    u->staging_path = staging_dir_ + "/" + u->id + ".part";
    u->hash = std::make_unique<Sha256>();
    int fd = ::open(u->staging_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return errno_to_status(errno, "staging");
    ::close(fd);
    id_out = u->id;
    uploads_[u->id] = std::move(u);
    return Status::good();
}

Status TransferManager::append(const std::string& id, const std::string& owner, std::uint64_t offset, const char* data, std::size_t len) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = uploads_.find(id);
    if (it == uploads_.end() || it->second->owner != owner) return Status::fail(Err::not_found, "upload not found");
    Upload& u = *it->second;
    if (u.state != "open") return Status::fail(Err::invalid_argument, "upload is " + u.state);
    if (offset != u.received) return Status::fail(Err::invalid_argument, "offset mismatch; resume at " + std::to_string(u.received));
    if (u.received + len > u.size) return Status::fail(Err::invalid_argument, "more data than announced");
    int fd = ::open(u.staging_path.c_str(), O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return errno_to_status(errno, "staging");
    std::size_t off = 0;
    while (off < len) {
        ssize_t w = ::write(fd, data + off, len - off);
        if (w < 0) { if (errno == EINTR) continue; Status s = errno_to_status(errno, "staging write"); ::close(fd); u.state = "failed"; u.error = s.message; return s; }
        off += std::size_t(w);
    }
    ::close(fd);
    u.hash->update(data, len);
    u.received += len; u.updated = now_ms();
    return Status::good();
}

Status TransferManager::finish(const std::string& id, const std::string& owner, std::string& staged_path, std::string& sha256) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = uploads_.find(id);
    if (it == uploads_.end() || it->second->owner != owner) return Status::fail(Err::not_found, "upload not found");
    Upload& u = *it->second;
    if (u.state != "open") return Status::fail(Err::invalid_argument, "upload is " + u.state);
    if (u.received != u.size) return Status::fail(Err::invalid_argument, "upload incomplete: " + std::to_string(u.received) + " of " + std::to_string(u.size) + " bytes");
    int fd = ::open(u.staging_path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0) { ::fsync(fd); ::close(fd); }
    u.final_sha256 = u.hash->hex();
    u.hash.reset();
    if (!u.expected_sha256.empty() && !constant_time_equal(u.expected_sha256, u.final_sha256)) {
        u.state = "failed"; u.error = "checksum mismatch";
        ::unlink(u.staging_path.c_str());
        return Status::fail(Err::io, "checksum mismatch; the upload was discarded");
    }
    u.state = "complete"; u.updated = now_ms();
    staged_path = u.staging_path; sha256 = u.final_sha256;
    return Status::good();
}

Status TransferManager::completed(const std::string& id, const std::string& owner, std::string& staged_path, std::string& sha256) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = uploads_.find(id);
    if (it == uploads_.end() || it->second->owner != owner) return Status::fail(Err::not_found, "upload not found");
    if (it->second->state != "complete") return Status::fail(Err::invalid_argument, "upload is " + it->second->state);
    staged_path = it->second->staging_path; sha256 = it->second->final_sha256;
    return Status::good();
}

Status TransferManager::abort(const std::string& id, const std::string& owner) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = uploads_.find(id);
    if (it == uploads_.end() || it->second->owner != owner) return Status::fail(Err::not_found, "upload not found");
    ::unlink(it->second->staging_path.c_str());
    it->second->state = "aborted"; it->second->updated = now_ms();
    return Status::good();
}

json TransferManager::get(const std::string& id, const std::string& owner) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = uploads_.find(id);
    if (it == uploads_.end() || it->second->owner != owner) return json();
    return it->second->to_json();
}

json TransferManager::list(const std::string& owner) const {
    std::lock_guard<std::mutex> lk(mu_);
    json out = json::array();
    for (const auto& [k, u] : uploads_) if (u->owner == owner) out.push_back(u->to_json());
    return out;
}

void TransferManager::forget(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    uploads_.erase(id);
}

std::size_t TransferManager::reap_stale(std::int64_t older_than_ms) {
    std::lock_guard<std::mutex> lk(mu_);
    std::size_t n = 0;
    std::int64_t cutoff = now_ms() - older_than_ms;
    for (auto it = uploads_.begin(); it != uploads_.end();) {
        if (it->second->updated < cutoff || it->second->state == "aborted" || it->second->state == "failed") {
            if (it->second->state != "complete") ::unlink(it->second->staging_path.c_str());
            it = uploads_.erase(it); ++n;
        } else ++it;
    }
    // Orphans from a previous process: any .part not tracked.
    DIR* d = ::opendir(staging_dir_.c_str());
    if (d) {
        while (dirent* e = ::readdir(d)) {
            std::string name = e->d_name;
            if (name.size() < 6 || name.substr(name.size() - 5) != ".part") continue;
            if (uploads_.count(name.substr(0, name.size() - 5))) continue;
            std::string p = staging_dir_ + "/" + name;
            struct stat st{};
            if (::lstat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (st.st_mtime * 1000) < cutoff) { ::unlink(p.c_str()); ++n; }
        }
        ::closedir(d);
    }
    return n;
}

} // namespace dm
