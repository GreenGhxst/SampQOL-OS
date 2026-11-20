#include "intelligence.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <sys/stat.h>
#include <unistd.h>

namespace dm {

namespace {
std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string ext_of(const std::string& n) { auto p = n.rfind('.'); return p == std::string::npos || p == 0 ? "" : lower(n.substr(p + 1)); }
std::string stem_of(const std::string& n) { auto p = n.rfind('.'); return p == std::string::npos || p == 0 ? n : n.substr(0, p); }
json file_json(const std::string& vol, const IndexedFile& f) {
    return json{{"volume", vol}, {"path", f.path}, {"name", f.name}, {"size", f.size}, {"mtime", f.mtime}, {"kind", f.kind}, {"sha256", f.sha256}};
}
} // namespace

Intelligence::Intelligence(std::string data_dir, VolumeRegistry& reg, FileOps& ops)
    : data_dir_(std::move(data_dir)), reg_(reg), ops_(ops) {}

void Intelligence::load() {
    std::lock_guard<std::mutex> lk(mu_);
    std::ifstream f(data_dir_ + "/intelligence.json");
    if (f) {
        json j = json::parse(f, nullptr, false);
        if (!j.is_discarded()) {
            if (j.contains("collections")) collections_ = j["collections"];
            for (const auto& s : j.value("favorites", std::vector<std::string>{})) favorites_.insert(s);
            for (const auto& r : j.value("recents", json::array())) recents_.push_back(r);
            json tags = j.value("tags", json::object());   // bind first: items() must not outlive a temporary
            for (auto& [k, v] : tags.items()) if (v.is_array()) tags_[k] = v.get<std::vector<std::string>>();
        }
    }
    for (const auto& v : reg_.list()) {
        std::ifstream fi(data_dir_ + "/index-" + v.id + ".json");
        if (!fi) continue;
        json j = json::parse(fi, nullptr, false);
        if (j.is_discarded()) continue;
        std::vector<IndexedFile> files;
        for (const auto& e : j.value("files", json::array())) {
            IndexedFile x; x.path = e.value("path", ""); x.name = e.value("name", ""); x.size = e.value("size", 0ull);
            x.mtime = e.value("mtime", 0ll); x.kind = e.value("kind", ""); x.sha256 = e.value("sha256", "");
            files.push_back(x);
        }
        index_[v.id] = std::move(files);
        indexed_at_[v.id] = j.value("indexed_at", 0ll);
        folder_counts_[v.id] = j.value("folders", 0ull);
    }
}

void Intelligence::save() { std::lock_guard<std::mutex> lk(mu_); save_locked(); }

void Intelligence::save_locked() {
    auto write_atomic = [&](const std::string& path, const json& j) {
        std::string tmp = path + ".tmp";
        std::ofstream o(tmp, std::ios::trunc);
        o << j.dump();
        o.close();
        ::rename(tmp.c_str(), path.c_str());
    };
    json tags = json::object();
    for (auto& [k, v] : tags_) tags[k] = v;
    write_atomic(data_dir_ + "/intelligence.json", json{{"collections", collections_},
        {"favorites", std::vector<std::string>(favorites_.begin(), favorites_.end())}, {"recents", recents_}, {"tags", tags}});
    for (auto& [vol, files] : index_) {
        json arr = json::array();
        for (const auto& f : files) arr.push_back(file_json(vol, f));
        write_atomic(data_dir_ + "/index-" + vol + ".json", json{{"files", arr}, {"indexed_at", indexed_at_[vol]}, {"folders", folder_counts_[vol]}});
    }
}

Status Intelligence::scan(const std::string& volume_id, std::atomic<bool>* cancel, std::size_t& indexed, unsigned throttle_us) {
    auto v = reg_.get(volume_id);
    if (!v) return Status::fail(Err::not_found, "unknown volume");
    Status st = PathAuthorizer::check_read(*v); if (!st.ok()) return st;
    int rfd = ::open(v->root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (rfd < 0) return Status::fail(Err::unavailable, "volume root unavailable");
    std::vector<IndexedFile> files; std::uint64_t folders = 0; indexed = 0;
    bool cancelled = false;
    std::function<void(int, const std::string&, int)> walk = [&](int fd, const std::string& rel, int depth) {
        if (depth > 32 || cancelled) return;
        DIR* d = ::fdopendir(::dup(fd));
        if (!d) return;
        while (dirent* e = ::readdir(d)) {
            if (cancel && cancel->load()) { cancelled = true; break; }
            if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..") || e->d_name[0] == '.') continue;
            std::string name = e->d_name;
            std::string path = rel.empty() ? name : rel + "/" + name;
            RelPath rp; rp.parts = {}; { std::string acc; for (std::size_t i = 0, j; i < path.size(); i = j + 1) { j = path.find('/', i); if (j == std::string::npos) j = path.size(); rp.parts.push_back(path.substr(i, j - i)); } }
            if (PathAuthorizer::is_protected(*v, rp)) continue;      // never index protected locations
            struct stat s{};
            if (::fstatat(fd, e->d_name, &s, AT_SYMLINK_NOFOLLOW) != 0) continue;
            if (S_ISREG(s.st_mode)) {
                IndexedFile f; f.path = path; f.name = name; f.size = std::uint64_t(s.st_size); f.mtime = s.st_mtime; f.kind = file_kind(name);
                files.push_back(std::move(f)); ++indexed;
                if ((indexed % 64) == 0 && throttle_us) ::usleep(throttle_us);
                if (files.size() >= 500000) { cancelled = true; break; }
            } else if (S_ISDIR(s.st_mode)) {
                ++folders;
                int c = ::openat(fd, e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (c >= 0) { walk(c, path, depth + 1); ::close(c); }
            }
        }
        ::closedir(d);
    };
    walk(rfd, "", 0);
    ::close(rfd);
    if (cancelled && cancel && cancel->load()) return Status::fail(Err::cancelled, "cancelled");
    std::lock_guard<std::mutex> lk(mu_);
    // keep known hashes
    std::map<std::string, std::string> old_hash;
    for (const auto& f : index_[volume_id]) if (!f.sha256.empty()) old_hash[f.path + "#" + std::to_string(f.size) + "#" + std::to_string(f.mtime)] = f.sha256;
    for (auto& f : files) { auto it = old_hash.find(f.path + "#" + std::to_string(f.size) + "#" + std::to_string(f.mtime)); if (it != old_hash.end()) f.sha256 = it->second; }
    index_[volume_id] = std::move(files);
    indexed_at_[volume_id] = now_ms();
    folder_counts_[volume_id] = folders;
    save_locked();
    return Status::good();
}

json Intelligence::index_status() const {
    std::lock_guard<std::mutex> lk(mu_);
    json out = json::array();
    for (const auto& [vol, files] : index_) {
        auto ia = indexed_at_.find(vol);
        out.push_back(json{{"volume", vol}, {"files", files.size()}, {"indexed_at", ia == indexed_at_.end() ? 0 : ia->second}});
    }
    return out;
}

json Intelligence::search(const SearchQuery& q) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::pair<std::string, const IndexedFile*>> hits;
    std::string text = lower(q.text), ext = lower(q.ext);
    for (const auto& [vol, files] : index_) {
        if (!q.volume.empty() && vol != q.volume) continue;
        for (const auto& f : files) {
            if (!text.empty() && lower(f.name).find(text) == std::string::npos && lower(f.path).find(text) == std::string::npos) continue;
            if (!q.kind.empty() && f.kind != q.kind) continue;
            if (!ext.empty() && ext_of(f.name) != ext) continue;
            if (q.min_size && f.size < q.min_size) continue;
            if (q.max_size && f.size > q.max_size) continue;
            hits.emplace_back(vol, &f);
        }
    }
    std::sort(hits.begin(), hits.end(), [&](const auto& a, const auto& b) {
        int c = 0;
        if (q.sort == "size") c = a.second->size < b.second->size ? -1 : a.second->size > b.second->size;
        else if (q.sort == "mtime") c = a.second->mtime < b.second->mtime ? -1 : a.second->mtime > b.second->mtime;
        if (c == 0) c = lower(a.second->name).compare(lower(b.second->name));
        return q.descending ? c > 0 : c < 0;
    });
    json out = json::array();
    std::size_t limit = std::min<std::size_t>(q.limit, 1000);
    for (std::size_t i = q.offset; i < hits.size() && out.size() < limit; ++i) out.push_back(file_json(hits[i].first, *hits[i].second));
    return json{{"total", hits.size()}, {"results", out}};
}

json Intelligence::dashboard_counts(const std::string& volume_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = index_.find(volume_id);
    json by_kind = json::object();
    std::uint64_t bytes = 0;
    if (it != index_.end()) for (const auto& f : it->second) {
        auto& k = by_kind[f.kind];
        if (k.is_null()) k = json{{"files", 0}, {"bytes", 0}};
        k["files"] = k["files"].get<std::uint64_t>() + 1; k["bytes"] = k["bytes"].get<std::uint64_t>() + f.size; bytes += f.size;
    }
    auto fc = folder_counts_.find(volume_id); auto ia = indexed_at_.find(volume_id);
    return json{{"volume", volume_id}, {"indexed", it != index_.end()}, {"files", it == index_.end() ? 0 : it->second.size()},
                {"folders", fc == folder_counts_.end() ? 0 : fc->second}, {"bytes", bytes}, {"by_kind", by_kind},
                {"indexed_at", ia == indexed_at_.end() ? 0 : ia->second}};
}

json Intelligence::duplicates(const std::string& volume_id, std::atomic<bool>* cancel, std::size_t max_hash) {
    auto v = reg_.get(volume_id);
    if (!v) return json{{"error", "unknown volume"}};
    std::vector<IndexedFile> snapshot;
    { std::lock_guard<std::mutex> lk(mu_); auto it = index_.find(volume_id); if (it != index_.end()) snapshot = it->second; }
    std::map<std::uint64_t, std::vector<std::size_t>> by_size;
    for (std::size_t i = 0; i < snapshot.size(); ++i) if (snapshot[i].size > 0) by_size[snapshot[i].size].push_back(i);
    std::size_t hashed = 0; bool truncated = false;
    for (auto& [size, idxs] : by_size) {
        if (idxs.size() < 2) continue;
        for (auto i : idxs) {
            if (!snapshot[i].sha256.empty()) continue;
            if (hashed >= max_hash) { truncated = true; break; }
            if (cancel && cancel->load()) return json{{"error", "cancelled"}};
            RelPath rp; if (!parse_rel_path(snapshot[i].path, rp).ok()) continue;
            std::string hex; OpContext ctx; ctx.cancel = cancel;
            if (ops_.sha256(*v, rp, hex, ctx).ok()) { snapshot[i].sha256 = hex; ++hashed; }
        }
    }
    std::map<std::string, std::vector<std::size_t>> by_hash;
    for (auto& [size, idxs] : by_size) if (idxs.size() >= 2) for (auto i : idxs) if (!snapshot[i].sha256.empty()) by_hash[snapshot[i].sha256 + "#" + std::to_string(size)].push_back(i);
    json groups = json::array();
    for (auto& [h, idxs] : by_hash) {
        if (idxs.size() < 2) continue;
        json files = json::array();
        for (auto i : idxs) files.push_back(file_json(volume_id, snapshot[i]));
        groups.push_back(json{{"sha256", snapshot[idxs[0]].sha256}, {"size", snapshot[idxs[0]].size}, {"files", files}});
    }
    {   // persist the hashes we computed
        std::lock_guard<std::mutex> lk(mu_);
        auto it = index_.find(volume_id);
        if (it != index_.end()) {
            std::map<std::string, std::string> hs; for (auto& f : snapshot) if (!f.sha256.empty()) hs[f.path] = f.sha256;
            for (auto& f : it->second) { auto x = hs.find(f.path); if (x != hs.end()) f.sha256 = x->second; }
            save_locked();
        }
    }
    return json{{"groups", groups}, {"hashed", hashed}, {"truncated", truncated}};
}

json Intelligence::collections() const { std::lock_guard<std::mutex> lk(mu_); return collections_; }

Status Intelligence::collection_create(const std::string& name, std::string& id) {
    if (name.empty() || name.size() > 80) return Status::fail(Err::invalid_argument, "bad collection name");
    std::lock_guard<std::mutex> lk(mu_);
    if (collections_.size() >= 200) return Status::fail(Err::busy, "too many collections");
    id = random_token(8);
    collections_.push_back(json{{"id", id}, {"name", name}, {"items", json::array()}, {"created", iso8601_now()}});
    save_locked();
    return Status::good();
}
Status Intelligence::collection_rename(const std::string& id, const std::string& name) {
    if (name.empty() || name.size() > 80) return Status::fail(Err::invalid_argument, "bad collection name");
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& c : collections_) if (c["id"] == id) { c["name"] = name; save_locked(); return Status::good(); }
    return Status::fail(Err::not_found, "collection not found");
}
Status Intelligence::collection_delete(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    for (std::size_t i = 0; i < collections_.size(); ++i) if (collections_[i]["id"] == id) { collections_.erase(i); save_locked(); return Status::good(); }
    return Status::fail(Err::not_found, "collection not found");
}
Status Intelligence::collection_add(const std::string& id, const std::string& volume, const std::string& path) {
    RelPath rp; Status st = parse_rel_path(path, rp); if (!st.ok()) return st;
    if (!reg_.get(volume)) return Status::fail(Err::not_found, "unknown volume");
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& c : collections_) if (c["id"] == id) {
        for (auto& it : c["items"]) if (it["volume"] == volume && it["path"] == path) return Status::good();
        if (c["items"].size() >= 5000) return Status::fail(Err::busy, "collection is full");
        c["items"].push_back(json{{"volume", volume}, {"path", path}, {"name", rp.leaf()}});
        save_locked(); return Status::good();
    }
    return Status::fail(Err::not_found, "collection not found");
}
Status Intelligence::collection_remove(const std::string& id, const std::string& volume, const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& c : collections_) if (c["id"] == id) {
        auto& items = c["items"];
        for (std::size_t i = 0; i < items.size(); ++i) if (items[i]["volume"] == volume && items[i]["path"] == path) { items.erase(i); save_locked(); return Status::good(); }
        return Status::fail(Err::not_found, "item not in collection");
    }
    return Status::fail(Err::not_found, "collection not found");
}

json Intelligence::favorites() const {
    std::lock_guard<std::mutex> lk(mu_);
    json out = json::array();
    for (const auto& k : favorites_) { auto p = k.find(':'); out.push_back(json{{"volume", k.substr(0, p)}, {"path", k.substr(p + 1)}}); }
    return out;
}
Status Intelligence::favorite(const std::string& volume, const std::string& path, bool on) {
    RelPath rp; Status st = parse_rel_path(path, rp); if (!st.ok()) return st;
    std::lock_guard<std::mutex> lk(mu_);
    if (on) { if (favorites_.size() >= 1000) return Status::fail(Err::busy, "too many favorites"); favorites_.insert(key(volume, path)); }
    else favorites_.erase(key(volume, path));
    save_locked();
    return Status::good();
}
void Intelligence::touch_recent(const std::string& volume, const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    recents_.erase(std::remove_if(recents_.begin(), recents_.end(), [&](const json& r) { return r["volume"] == volume && r["path"] == path; }), recents_.end());
    recents_.insert(recents_.begin(), json{{"volume", volume}, {"path", path}, {"ts", now_ms()}});
    if (recents_.size() > 50) recents_.resize(50);
    save_locked();
}
json Intelligence::recents() const { std::lock_guard<std::mutex> lk(mu_); return recents_; }
json Intelligence::tags(const std::string& volume, const std::string& path) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = tags_.find(key(volume, path));
    return it == tags_.end() ? json::array() : json(it->second);
}
Status Intelligence::set_tags(const std::string& volume, const std::string& path, const std::vector<std::string>& tags) {
    RelPath rp; Status st = parse_rel_path(path, rp); if (!st.ok()) return st;
    if (tags.size() > 32) return Status::fail(Err::invalid_argument, "too many tags");
    for (const auto& t : tags) if (t.empty() || t.size() > 40) return Status::fail(Err::invalid_argument, "bad tag");
    std::lock_guard<std::mutex> lk(mu_);
    if (tags.empty()) tags_.erase(key(volume, path)); else tags_[key(volume, path)] = tags;
    save_locked();
    return Status::good();
}
json Intelligence::all_tags() const {
    std::lock_guard<std::mutex> lk(mu_);
    json out = json::object();
    for (auto& [k, v] : tags_) out[k] = v;
    return out;
}

std::vector<RenamePlan> Intelligence::rename_plan(const VolumeInfo& v, const RelPath& dir, const std::vector<std::string>& names,
                                                  const std::string& pattern, int start) const {
    std::vector<RenamePlan> plan;
    std::set<std::string> targets;
    std::time_t t = std::time(nullptr); char date[16]; std::tm tm{}; localtime_r(&t, &tm); std::strftime(date, sizeof date, "%Y%m%d", &tm);
    int n = start;
    for (const auto& name : names) {
        RenamePlan p; p.from = name;
        std::string out;
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            if (pattern[i] == '{') {
                auto e = pattern.find('}', i);
                if (e == std::string::npos) { out += pattern.substr(i); break; }
                std::string tok = pattern.substr(i + 1, e - i - 1);
                if (tok == "name") out += stem_of(name);
                else if (tok == "ext") out += ext_of(name).empty() ? "" : "." + ext_of(name);
                else if (tok == "date") out += date;
                else if (tok.rfind("n", 0) == 0) { int width = tok.size() > 1 ? std::atoi(tok.c_str() + 1) : 1; char b[16]; std::snprintf(b, sizeof b, "%0*d", width, n); out += b; }
                else out += "{" + tok + "}";
                i = e;
            } else out.push_back(pattern[i]);
        }
        ++n;
        p.to = out;
        Status st = validate_name(out);
        if (!st.ok()) p.problem = st.message;
        else if (!targets.insert(lower(out)).second) p.problem = "duplicate target name";
        else if (PathAuthorizer::is_protected(v, dir.child(name))) p.problem = "protected";
        else if (out != name) {
            Resolved r;
            if (PathAuthorizer::resolve(v, dir.child(out), r, false).ok() && r.exists) p.problem = "a file with the new name exists";
        }
        plan.push_back(p);
    }
    return plan;
}

json Intelligence::rename_apply(const VolumeInfo& v, const RelPath& dir, const std::vector<RenamePlan>& plan) {
    json out = json::array();
    for (const auto& p : plan) {
        if (!p.problem.empty()) { out.push_back(json{{"from", p.from}, {"to", p.to}, {"state", "skipped"}, {"error", p.problem}}); continue; }
        if (p.from == p.to) { out.push_back(json{{"from", p.from}, {"to", p.to}, {"state", "done"}}); continue; }
        Status st = ops_.rename(v, dir.child(p.from), p.to);
        if (st.ok()) invalidate(v.id, dir.child(p.from).str());
        out.push_back(json{{"from", p.from}, {"to", p.to}, {"state", st.ok() ? "done" : "failed"}, {"error", st.message}});
    }
    return out;
}

void Intelligence::invalidate(const std::string& volume, const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = index_.find(volume);
    if (it == index_.end()) return;
    auto& files = it->second;
    files.erase(std::remove_if(files.begin(), files.end(), [&](const IndexedFile& f) {
        return f.path == path || f.path.rfind(path + "/", 0) == 0; }), files.end());
}

} // namespace dm
