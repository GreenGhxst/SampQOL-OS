// File Intelligence: metadata/search index, collections, favorites, recents,
// tags, duplicate finder and batch rename. Data lives in the DISKMODE
// application data directory (never inside firmware or protected paths);
// virtual collections only reference files, they never move them.
#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "../util/common.h"
#include "file_ops.h"
#include "volume_registry.h"

namespace dm {

struct IndexedFile {
    std::string path;      // relative to the volume root
    std::string name;
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
    std::string kind;      // audio | project | other
    std::string sha256;    // lazily filled by the duplicate finder
};

struct SearchQuery {
    std::string volume;    // "" = all indexed volumes
    std::string text;      // case-insensitive substring of the name
    std::string kind;      // "" | audio | project | other
    std::string ext;       // without dot
    std::uint64_t min_size = 0, max_size = 0;
    std::string sort = "name";  // name | size | mtime
    bool descending = false;
    std::size_t offset = 0, limit = 200;
};

struct RenamePlan { std::string from, to, problem; };

class Intelligence {
public:
    Intelligence(std::string data_dir, VolumeRegistry& reg, FileOps& ops);
    void load();
    void save();

    // Indexing (run from a job). Walks the volume with a small sleep every
    // N entries so the sampler's own threads are not starved.
    Status scan(const std::string& volume_id, std::atomic<bool>* cancel, std::size_t& indexed, unsigned throttle_us = 200);
    json index_status() const;
    json search(const SearchQuery& q) const;
    json dashboard_counts(const std::string& volume_id) const;   // files/folders/bytes by kind
    // Groups of identical files (size + sha256). Hashes at most `max_hash`
    // candidates per call so it stays bounded.
    json duplicates(const std::string& volume_id, std::atomic<bool>* cancel, std::size_t max_hash = 500);

    json collections() const;
    Status collection_create(const std::string& name, std::string& id);
    Status collection_rename(const std::string& id, const std::string& name);
    Status collection_delete(const std::string& id);
    Status collection_add(const std::string& id, const std::string& volume, const std::string& path);
    Status collection_remove(const std::string& id, const std::string& volume, const std::string& path);

    json favorites() const;
    Status favorite(const std::string& volume, const std::string& path, bool on);
    void touch_recent(const std::string& volume, const std::string& path);
    json recents() const;
    json tags(const std::string& volume, const std::string& path) const;
    Status set_tags(const std::string& volume, const std::string& path, const std::vector<std::string>& tags);
    json all_tags() const;

    // Batch rename: pattern tokens {name} (stem), {ext}, {n} (counter from `start`),
    // {date}. Returns a plan; `apply` performs it through FileOps.
    std::vector<RenamePlan> rename_plan(const VolumeInfo& v, const RelPath& dir, const std::vector<std::string>& names,
                                        const std::string& pattern, int start) const;
    json rename_apply(const VolumeInfo& v, const RelPath& dir, const std::vector<RenamePlan>& plan);
    // Drops index entries under a path that was renamed/moved/deleted.
    void invalidate(const std::string& volume, const std::string& path);

private:
    static std::string key(const std::string& volume, const std::string& path) { return volume + ":" + path; }
    void save_locked();
    std::string data_dir_;
    VolumeRegistry& reg_;
    FileOps& ops_;
    mutable std::mutex mu_;
    std::map<std::string, std::vector<IndexedFile>> index_;      // volume -> files
    std::map<std::string, std::int64_t> indexed_at_;
    std::map<std::string, std::uint64_t> folder_counts_;
    json collections_ = json::array();
    std::set<std::string> favorites_;
    std::vector<json> recents_;
    std::map<std::string, std::vector<std::string>> tags_;
};

} // namespace dm
