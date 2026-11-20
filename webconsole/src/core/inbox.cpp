#include "inbox.h"
#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace dm {

json InboxItem::to_json() const {
    return json{{"id", id}, {"name", name}, {"size", size}, {"sha256", sha256}, {"state", state}, {"received_at", received_at},
                {"note", note}, {"wav", wav.to_json()}, {"duplicates", duplicates}, {"kind", file_kind(name)}};
}

Inbox::Inbox(std::string data_dir, FileOps& ops, Intelligence& intel, VolumeRegistry& reg)
    : data_dir_(std::move(data_dir)), inbox_dir_(data_dir_ + "/inbox"), ops_(ops), intel_(intel), reg_(reg) {
    ::mkdir(inbox_dir_.c_str(), 0700);
}

void Inbox::load() {
    std::lock_guard<std::mutex> lk(mu_);
    std::ifstream f(data_dir_ + "/inbox.json");
    if (!f) return;
    json j = json::parse(f, nullptr, false);
    if (j.is_discarded()) return;
    for (const auto& e : j.value("items", json::array())) {
        InboxItem it; it.id = e.value("id", ""); it.name = e.value("name", ""); it.staged_path = e.value("staged_path", "");
        it.sha256 = e.value("sha256", ""); it.state = e.value("state", "staged"); it.received_at = e.value("received_at", "");
        it.size = e.value("size", 0ull); it.note = e.value("note", "");
        struct stat st{};
        if (it.staged_path.rfind(inbox_dir_ + "/", 0) != 0 || ::lstat(it.staged_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        int fd = ::open(it.staged_path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd >= 0) { it.wav = wav_probe(fd); ::close(fd); }
        items_.push_back(it);
    }
}

void Inbox::save_locked() {
    json arr = json::array();
    for (const auto& it : items_) { json j = it.to_json(); j["staged_path"] = it.staged_path; arr.push_back(j); }
    std::string tmp = data_dir_ + "/inbox.json.tmp";
    std::ofstream o(tmp, std::ios::trunc); o << json{{"items", arr}}.dump(); o.close();
    ::rename(tmp.c_str(), (data_dir_ + "/inbox.json").c_str());
}

void Inbox::probe(InboxItem& it) {
    int fd = ::open(it.staged_path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0) { it.wav = wav_probe(fd); ::close(fd); }
    it.duplicates = json::array();
    // Duplicate detection: same size in the index, then a hash comparison
    // (bounded to a few candidates).
    SearchQuery q; q.min_size = it.size; q.max_size = it.size; q.limit = 20;
    json hits = intel_.search(q);
    std::size_t hashed = 0;
    for (const auto& h : hits["results"]) {
        std::string hs = h.value("sha256", "");
        if (hs.empty() && hashed < 8) {
            auto v = reg_.get(h["volume"]); RelPath rp;
            if (v && parse_rel_path(h["path"], rp).ok()) { OpContext ctx; ops_.sha256(*v, rp, hs, ctx); ++hashed; }
        }
        if (!hs.empty() && hs == it.sha256) it.duplicates.push_back(json{{"volume", h["volume"]}, {"path", h["path"]}});
    }
}

Status Inbox::add(const std::string& staged_path, const std::string& name, const std::string& sha256, std::string& id) {
    Status st = validate_name(name); if (!st.ok()) return st;
    struct stat s{};
    if (::lstat(staged_path.c_str(), &s) != 0 || !S_ISREG(s.st_mode)) return Status::fail(Err::not_found, "staged file missing");
    std::lock_guard<std::mutex> lk(mu_);
    if (items_.size() >= 500) return Status::fail(Err::busy, "inbox is full; commit or discard items first");
    InboxItem it; it.id = random_token(8); it.name = name; it.sha256 = sha256; it.size = std::uint64_t(s.st_size); it.received_at = iso8601_now();
    it.staged_path = inbox_dir_ + "/" + it.id + ".bin";
    if (::rename(staged_path.c_str(), it.staged_path.c_str()) != 0) {
        // staging and inbox live in the same data dir, so this is unexpected
        return errno_to_status(errno, "inbox");
    }
    probe(it);
    if (file_kind(name) != "audio") it.note = "not an audio file by extension";
    else if (!it.wav.valid) it.note = "not a WAV file; preview unavailable";
    else if (!it.wav.supported) it.note = it.wav.note;
    id = it.id;
    items_.push_back(std::move(it));
    save_locked();
    return Status::good();
}

json Inbox::list() const { std::lock_guard<std::mutex> lk(mu_); json a = json::array(); for (auto& it : items_) a.push_back(it.to_json()); return a; }
json Inbox::get(const std::string& id) const { std::lock_guard<std::mutex> lk(mu_); for (auto& it : items_) if (it.id == id) return it.to_json(); return json(); }
std::size_t Inbox::count() const { std::lock_guard<std::mutex> lk(mu_); return items_.size(); }

Status Inbox::rename(const std::string& id, const std::string& name) {
    Status st = validate_name(name); if (!st.ok()) return st;
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& it : items_) if (it.id == id) { it.name = name; save_locked(); return Status::good(); }
    return Status::fail(Err::not_found, "inbox item not found");
}

Status Inbox::discard(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    for (std::size_t i = 0; i < items_.size(); ++i) if (items_[i].id == id) {
        ::unlink(items_[i].staged_path.c_str()); items_.erase(items_.begin() + i); save_locked(); return Status::good();
    }
    return Status::fail(Err::not_found, "inbox item not found");
}

Status Inbox::peaks(const std::string& id, std::size_t buckets, std::vector<float>& out) const {
    std::string path; WavInfo w;
    { std::lock_guard<std::mutex> lk(mu_); for (auto& it : items_) if (it.id == id) { path = it.staged_path; w = it.wav; } }
    if (path.empty()) return Status::fail(Err::not_found, "inbox item not found");
    int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return errno_to_status(errno, "open");
    Status st = wav_peaks(fd, w, buckets, out);
    ::close(fd);
    return st;
}

Status Inbox::open(const std::string& id, ScopedFd& fd, std::uint64_t& size, std::string& name) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& it : items_) if (it.id == id) {
        int f = ::open(it.staged_path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (f < 0) return errno_to_status(errno, "open");
        fd.reset(f); size = it.size; name = it.name; return Status::good();
    }
    return Status::fail(Err::not_found, "inbox item not found");
}

Status Inbox::commit(const std::string& id, const VolumeInfo& v, const RelPath& dir, Conflict c, std::string& final_path) {
    std::lock_guard<std::mutex> lk(mu_);
    for (std::size_t i = 0; i < items_.size(); ++i) if (items_[i].id == id) {
        std::string fname; OpContext ctx;
        Status st = ops_.place_file(items_[i].staged_path, v, dir, items_[i].name, c, ctx, &fname);
        if (!st.ok()) return st;
        final_path = dir.child(fname).str();
        items_.erase(items_.begin() + i);
        save_locked();
        return Status::good();
    }
    return Status::fail(Err::not_found, "inbox item not found");
}

} // namespace dm
