// SPDX-License-Identifier: MIT
#include "mpcvst/plugin_registry.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace mpcvst {

namespace {

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else out.push_back(c);
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            out.push_back(n == 't' ? '\t' : n == 'n' ? '\n' : n);
        } else out.push_back(s[i]);
    }
    return out;
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        size_t end = line.find('\t', start);
        if (end == std::string::npos) { fields.push_back(line.substr(start)); break; }
        fields.push_back(line.substr(start, end - start));
        start = end + 1;
    }
    return fields;
}

PluginStatus status_from(const std::string& s) {
    if (s == "ok") return PluginStatus::Ok;
    if (s == "rejected") return PluginStatus::Rejected;
    if (s == "quarantined") return PluginStatus::Quarantined;
    return PluginStatus::Unknown;
}

PluginKind kind_from(const std::string& s) {
    if (s == "instrument") return PluginKind::Instrument;
    if (s == "effect") return PluginKind::Effect;
    return PluginKind::Unknown;
}

long long to_ll(const std::string& s) { return std::strtoll(s.c_str(), nullptr, 10); }

} // namespace

const char* plugin_status_name(PluginStatus status) {
    switch (status) {
    case PluginStatus::Ok: return "ok";
    case PluginStatus::Rejected: return "rejected";
    case PluginStatus::Quarantined: return "quarantined";
    default: return "unknown";
    }
}

std::string PluginRegistry::serialize(const PluginRecord& r) {
    std::ostringstream o;
    o << "1\t" << escape(r.path) << '\t' << r.file_size << '\t' << r.mtime << '\t' << plugin_status_name(r.status) << '\t'
      << escape(r.reason) << '\t' << r.crash_count << '\t' << r.last_scan << '\t'
      << escape(r.info.name) << '\t' << escape(r.info.vendor) << '\t' << r.info.unique_id << '\t' << r.info.version << '\t'
      << plugin_kind_name(r.info.kind) << '\t' << r.info.num_inputs << '\t' << r.info.num_outputs << '\t'
      << r.info.num_params << '\t' << r.info.num_programs << '\t' << r.info.initial_delay << '\t'
      << (r.info.has_chunks ? 1 : 0) << '\t' << (r.info.receives_midi ? 1 : 0) << '\t' << r.info.flags;
    return o.str();
}

bool PluginRegistry::parse(const std::string& line, PluginRecord& r) {
    std::vector<std::string> f = split_tabs(line);
    if (f.size() < 21 || f[0] != "1") return false;
    r = PluginRecord{};
    r.path = unescape(f[1]);
    r.file_size = static_cast<uint64_t>(to_ll(f[2]));
    r.mtime = to_ll(f[3]);
    r.status = status_from(f[4]);
    r.reason = unescape(f[5]);
    r.crash_count = static_cast<int>(to_ll(f[6]));
    r.last_scan = to_ll(f[7]);
    r.info.path = r.path;
    r.info.name = unescape(f[8]);
    r.info.vendor = unescape(f[9]);
    r.info.unique_id = static_cast<int32_t>(to_ll(f[10]));
    r.info.version = static_cast<int32_t>(to_ll(f[11]));
    r.info.kind = kind_from(f[12]);
    r.info.num_inputs = static_cast<int32_t>(to_ll(f[13]));
    r.info.num_outputs = static_cast<int32_t>(to_ll(f[14]));
    r.info.num_params = static_cast<int32_t>(to_ll(f[15]));
    r.info.num_programs = static_cast<int32_t>(to_ll(f[16]));
    r.info.initial_delay = static_cast<int32_t>(to_ll(f[17]));
    r.info.has_chunks = f[18] == "1";
    r.info.receives_midi = f[19] == "1";
    r.info.flags = static_cast<int32_t>(to_ll(f[20]));
    r.info.is_synth = (r.info.flags & (1 << 8)) != 0;
    return !r.path.empty();
}

bool PluginRegistry::load(const std::string& cache_file, std::string& error) {
    records_.clear();
    std::ifstream in(cache_file);
    if (!in) {
        error = "no cache file";
        return false;
    }
    std::string line;
    int bad = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        PluginRecord r;
        if (parse(line, r)) put(r);
        else ++bad;
    }
    if (bad) error = std::to_string(bad) + " unreadable line(s) skipped";
    return true;
}

bool PluginRegistry::save(const std::string& cache_file, std::string& error) const {
    std::string tmp = cache_file + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            error = "cannot write " + tmp;
            return false;
        }
        out << "# mpcvst plug-in registry. One plug-in per line; delete the file to rescan everything.\n";
        for (const PluginRecord& r : records_) out << serialize(r) << '\n';
        if (!out) {
            error = "write failed";
            return false;
        }
    }
    if (std::rename(tmp.c_str(), cache_file.c_str()) != 0) {
        error = std::string("rename failed: ") + std::strerror(errno);
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

PluginRecord* PluginRegistry::find(const std::string& path) {
    for (PluginRecord& r : records_) if (r.path == path) return &r;
    return nullptr;
}

const PluginRecord* PluginRegistry::find(const std::string& path) const {
    for (const PluginRecord& r : records_) if (r.path == path) return &r;
    return nullptr;
}

void PluginRegistry::put(const PluginRecord& record) {
    if (PluginRecord* existing = find(record.path)) *existing = record;
    else records_.push_back(record);
}

void PluginRegistry::remove(const std::string& path) {
    for (size_t i = 0; i < records_.size(); ++i)
        if (records_[i].path == path) { records_.erase(records_.begin() + static_cast<long>(i)); return; }
}

std::vector<const PluginRecord*> PluginRegistry::by_status(PluginStatus status) const {
    std::vector<const PluginRecord*> out;
    for (const PluginRecord& r : records_) if (r.status == status) out.push_back(&r);
    return out;
}

std::vector<const PluginRecord*> PluginRegistry::loadable(PluginKind kind) const {
    std::vector<const PluginRecord*> out;
    for (const PluginRecord& r : records_)
        if (r.status == PluginStatus::Ok && (kind == PluginKind::Unknown || r.info.kind == kind)) out.push_back(&r);
    return out;
}

void PluginRegistry::quarantine(const std::string& path, const std::string& reason) {
    PluginRecord* r = find(path);
    if (!r) {
        PluginRecord fresh;
        fresh.path = path;
        put(fresh);
        r = find(path);
    }
    r->status = PluginStatus::Quarantined;
    r->reason = reason;
    r->crash_count += 1;
}

bool PluginRegistry::quarantined(const std::string& path) const {
    const PluginRecord* r = find(path);
    return r && r->status == PluginStatus::Quarantined;
}

void PluginRegistry::clear_quarantine(const std::string& path) {
    if (PluginRecord* r = find(path)) {
        if (r->status == PluginStatus::Quarantined) {
            r->status = PluginStatus::Unknown;
            r->reason.clear();
            r->file_size = 0; // force a fresh probe
        }
    }
}

} // namespace mpcvst
