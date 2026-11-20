// SPDX-License-Identifier: MIT
// The plug-in registry: what the scanner found in the approved folders,
// cached on the card so the next start does not probe again, and the
// quarantine of plug-ins that crashed or hung.
#pragma once

#include "mpcvst/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mpcvst {

enum class PluginStatus : uint8_t {
    Unknown,
    Ok,            // validated by the worker: loads, opens, processes one block
    Rejected,      // not a loadable VST2 for this device (reason says why)
    Quarantined,   // crashed or hung in the worker, or crashed in the host
};

const char* plugin_status_name(PluginStatus status);

struct PluginRecord {
    std::string path;
    uint64_t file_size = 0;
    int64_t mtime = 0;             // seconds
    PluginStatus status = PluginStatus::Unknown;
    std::string reason;            // for Rejected and Quarantined
    PluginInfo info;               // valid when status is Ok
    int crash_count = 0;
    int64_t last_scan = 0;         // unix seconds
    bool same_file(uint64_t size, int64_t mtime_s) const { return file_size == size && mtime == mtime_s; }
};

class PluginRegistry {
public:
    // Loads the cache file; a missing or unreadable file is an empty registry.
    bool load(const std::string& cache_file, std::string& error);
    bool save(const std::string& cache_file, std::string& error) const;

    PluginRecord* find(const std::string& path);
    const PluginRecord* find(const std::string& path) const;
    void put(const PluginRecord& record);
    void remove(const std::string& path);
    const std::vector<PluginRecord>& records() const { return records_; }
    std::vector<PluginRecord>& records() { return records_; }

    std::vector<const PluginRecord*> by_status(PluginStatus status) const;
    std::vector<const PluginRecord*> loadable(PluginKind kind = PluginKind::Unknown) const;

    // Quarantine: a plug-in stays out until the user clears it.
    void quarantine(const std::string& path, const std::string& reason);
    bool quarantined(const std::string& path) const;
    void clear_quarantine(const std::string& path);

    // One record per line, fields separated by tabs with \ escapes. Plain
    // text so a person can read the cache on a computer.
    static std::string serialize(const PluginRecord& record);
    static bool parse(const std::string& line, PluginRecord& record);

private:
    std::vector<PluginRecord> records_;
};

} // namespace mpcvst
