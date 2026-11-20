// Smart Sample Inbox: uploads land in a staging area, get probed (WAV
// metadata, waveform peaks, duplicate check against the index) and are only
// then committed into an approved sample directory through FileOps.
#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "../util/common.h"
#include "file_ops.h"
#include "intelligence.h"
#include "wav.h"

namespace dm {

struct InboxItem {
    std::string id, name, staged_path, sha256, state = "staged", received_at, note;
    std::uint64_t size = 0;
    WavInfo wav;
    json duplicates = json::array();   // [{volume,path}]
    json to_json() const;
};

class Inbox {
public:
    Inbox(std::string data_dir, FileOps& ops, Intelligence& intel, VolumeRegistry& reg);
    void load();
    // Takes ownership of a complete staged upload (moves it into the inbox dir).
    Status add(const std::string& staged_path, const std::string& name, const std::string& sha256, std::string& id);
    json list() const;
    json get(const std::string& id) const;
    Status rename(const std::string& id, const std::string& name);
    Status discard(const std::string& id);
    Status peaks(const std::string& id, std::size_t buckets, std::vector<float>& out) const;
    Status open(const std::string& id, ScopedFd& fd, std::uint64_t& size, std::string& name) const;
    // Places the staged file at volume:dir/name. The original is never
    // overwritten unless the caller explicitly chooses that policy.
    Status commit(const std::string& id, const VolumeInfo& v, const RelPath& dir, Conflict c, std::string& final_path);
    std::size_t count() const;

private:
    void save_locked();
    void probe(InboxItem& it);
    std::string data_dir_, inbox_dir_;
    FileOps& ops_;
    Intelligence& intel_;
    VolumeRegistry& reg_;
    mutable std::mutex mu_;
    std::vector<InboxItem> items_;
};

} // namespace dm
