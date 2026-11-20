// Background Job Queue: bounded worker pool with cancellation and per-item
// results so batch operations can report completed / failed / cancelled /
// partial outcomes accurately.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../util/common.h"

namespace dm {

struct JobItem {
    std::string source, destination, state = "queued", error;
    json to_json() const { return json{{"source", source}, {"destination", destination}, {"state", state}, {"error", error}}; }
};

struct Job {
    std::string id, type, owner, state = "queued", error;
    std::int64_t created = 0, started = 0, finished = 0;
    std::atomic<std::uint64_t> done{0}, total{0};
    std::atomic<bool> cancel{false};
    std::vector<JobItem> items;      // guarded by JobQueue mutex when read from outside
    json detail;                     // free-form result (e.g. hash)
    json to_json() const;
};

class JobQueue {
public:
    using Fn = std::function<void(Job&)>;
    explicit JobQueue(std::size_t workers = 2, std::size_t max_queued = 64);
    ~JobQueue();
    // Returns Err::busy when the queue is full.
    Status submit(const std::string& type, const std::string& owner, Fn fn, std::string& id_out,
                  std::vector<JobItem> items = {});
    bool cancel(const std::string& id, const std::string& owner, bool admin);
    json get(const std::string& id) const;
    json list(const std::string& owner, bool all) const;
    bool any_running_on(const std::string& volume_id) const;   // used by the format preflight
    void set_item(Job& j, std::size_t idx, const std::string& state, const std::string& error = "");
    void shutdown();

private:
    void worker();
    struct Pending { std::shared_ptr<Job> job; Fn fn; };
    std::size_t max_queued_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Pending> queue_;
    std::map<std::string, std::shared_ptr<Job>> jobs_;
    std::deque<std::string> order_;
    std::vector<std::thread> threads_;
    bool stop_ = false;
    std::size_t seq_ = 0;
};

} // namespace dm
