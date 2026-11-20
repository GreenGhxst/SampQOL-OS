// Recovery Journal: append-only JSON-lines record of every mutating file
// operation. Each operation writes a "started" line (with any temporary
// file it will create) and a terminal line. On startup, operations that
// never reached a terminal state are marked "interrupted" and their
// temporary files are removed.
#pragma once
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "../util/common.h"

namespace dm {

struct JournalEntry {
    std::string id, op, state, source, destination, detail, ts;
    std::vector<std::string> temp_files; // absolute paths, only for cleanup after a crash
    json to_json() const;
};

class Journal {
public:
    explicit Journal(std::string path);          // e.g. <data>/journal.jsonl
    // Returns the id of the new entry.
    std::string begin(const std::string& op, const std::string& source, const std::string& destination,
                      const std::vector<std::string>& temp_files = {});
    void finish(const std::string& id, const std::string& state, const std::string& detail = "");
    // Scans the file, marks dangling "started" entries as interrupted and
    // returns the temporary files they registered.
    std::vector<std::string> recover();
    std::vector<JournalEntry> recent(std::size_t max = 100) const;
    const std::string& path() const { return path_; }

private:
    void append(const json& j);
    std::string path_;
    mutable std::mutex mu_;
    std::deque<JournalEntry> recent_;
    std::size_t seq_ = 0;
};

} // namespace dm
