#include "journal.h"
#include <fcntl.h>
#include <fstream>
#include <map>
#include <unistd.h>

namespace dm {

json JournalEntry::to_json() const {
    return json{{"id", id}, {"op", op}, {"state", state}, {"source", source}, {"destination", destination},
                {"detail", detail}, {"ts", ts}};
}

Journal::Journal(std::string path) : path_(std::move(path)) {}

void Journal::append(const json& j) {
    int fd = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) { log_error("journal: cannot open " + path_); return; }
    std::string line = j.dump() + "\n";
    ssize_t n = ::write(fd, line.data(), line.size());
    if (n != (ssize_t)line.size()) log_error("journal: short write");
    ::fdatasync(fd);
    ::close(fd);
}

std::string Journal::begin(const std::string& op, const std::string& source, const std::string& destination,
                           const std::vector<std::string>& temp_files) {
    JournalEntry e;
    {
        std::lock_guard<std::mutex> lk(mu_);
        e.id = std::to_string(now_ms()) + "-" + std::to_string(++seq_);
    }
    e.op = op; e.state = "started"; e.source = source; e.destination = destination; e.ts = iso8601_now();
    e.temp_files = temp_files;
    json j = e.to_json(); j["temp_files"] = temp_files;
    append(j);
    std::lock_guard<std::mutex> lk(mu_);
    recent_.push_back(e);
    if (recent_.size() > 500) recent_.pop_front();
    return e.id;
}

void Journal::finish(const std::string& id, const std::string& state, const std::string& detail) {
    json j{{"id", id}, {"state", state}, {"detail", detail}, {"ts", iso8601_now()}};
    append(j);
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& e : recent_) if (e.id == id) { e.state = state; e.detail = detail; e.ts = j["ts"]; }
}

std::vector<std::string> Journal::recover() {
    std::map<std::string, JournalEntry> open;
    std::ifstream f(path_);
    std::string line;
    std::vector<JournalEntry> ordered;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.contains("id")) continue;
        std::string id = j["id"];
        std::string state = j.value("state", "");
        if (state == "started") {
            JournalEntry e; e.id = id; e.op = j.value("op", ""); e.state = state;
            e.source = j.value("source", ""); e.destination = j.value("destination", ""); e.ts = j.value("ts", "");
            e.temp_files = j.value("temp_files", std::vector<std::string>{});
            open[id] = e; ordered.push_back(e);
        } else {
            auto it = open.find(id);
            if (it != open.end()) { it->second.state = state; it->second.detail = j.value("detail", ""); }
        }
    }
    std::vector<std::string> temps;
    for (auto& [id, e] : open) {
        if (e.state != "started") continue;
        for (auto& t : e.temp_files) temps.push_back(t);
        finish(id, "interrupted", "found unfinished at startup");
        e.state = "interrupted";
    }
    std::lock_guard<std::mutex> lk(mu_);
    recent_.clear();
    for (auto& e : ordered) {
        auto it = open.find(e.id);
        recent_.push_back(it != open.end() ? it->second : e);
    }
    while (recent_.size() > 500) recent_.pop_front();
    return temps;
}

std::vector<JournalEntry> Journal::recent(std::size_t max) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<JournalEntry> v;
    for (auto it = recent_.rbegin(); it != recent_.rend() && v.size() < max; ++it) v.push_back(*it);
    return v;
}

} // namespace dm
