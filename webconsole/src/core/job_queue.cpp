#include "job_queue.h"

namespace dm {

json Job::to_json() const {
    json items_j = json::array();
    std::size_t ok = 0, failed = 0, cancelled = 0, partial = 0;
    for (const auto& i : items) {
        items_j.push_back(i.to_json());
        if (i.state == "done") ++ok; else if (i.state == "failed") ++failed; else if (i.state == "cancelled") ++cancelled; else if (i.state == "partial") ++partial;
    }
    return json{{"id", id}, {"type", type}, {"state", state}, {"error", error}, {"created", created},
                {"started", started}, {"finished", finished}, {"done", done.load()}, {"total", total.load()},
                {"items", items_j}, {"summary", {{"done", ok}, {"failed", failed}, {"cancelled", cancelled}, {"partial", partial}, {"total", items.size()}}},
                {"detail", detail}};
}

JobQueue::JobQueue(std::size_t workers, std::size_t max_queued) : max_queued_(max_queued) {
    for (std::size_t i = 0; i < std::max<std::size_t>(1, workers); ++i) threads_.emplace_back([this] { worker(); });
}

JobQueue::~JobQueue() { shutdown(); }

void JobQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stop_) return;
        stop_ = true;
        for (auto& p : queue_) { p.job->state = "cancelled"; p.job->finished = now_ms(); }
        queue_.clear();
        for (auto& [id, j] : jobs_) if (j->state == "running") j->cancel = true;
    }
    cv_.notify_all();
    for (auto& t : threads_) if (t.joinable()) t.join();
}

Status JobQueue::submit(const std::string& type, const std::string& owner, Fn fn, std::string& id_out, std::vector<JobItem> items) {
    auto job = std::make_shared<Job>();
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stop_) return Status::fail(Err::unavailable, "shutting down");
        if (queue_.size() >= max_queued_) return Status::fail(Err::busy, "too many queued operations; try again later");
        job->id = std::to_string(now_ms()) + "-" + std::to_string(++seq_);
        job->type = type; job->owner = owner; job->created = now_ms(); job->items = std::move(items);
        jobs_[job->id] = job; order_.push_back(job->id);
        while (order_.size() > 200) {
            auto it = jobs_.find(order_.front());
            if (it != jobs_.end() && (it->second->state == "queued" || it->second->state == "running")) break;
            if (it != jobs_.end()) jobs_.erase(it);
            order_.pop_front();
        }
        queue_.push_back({job, std::move(fn)});
    }
    cv_.notify_one();
    id_out = job->id;
    return Status::good();
}

void JobQueue::worker() {
    for (;;) {
        Pending p;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            p = std::move(queue_.front()); queue_.pop_front();
            if (p.job->cancel) { p.job->state = "cancelled"; p.job->finished = now_ms(); continue; }
            p.job->state = "running"; p.job->started = now_ms();
        }
        try { p.fn(*p.job); }
        catch (const std::exception& e) { std::lock_guard<std::mutex> lk(mu_); p.job->state = "failed"; p.job->error = e.what(); }
        std::lock_guard<std::mutex> lk(mu_);
        if (p.job->state == "running") {
            std::size_t failed = 0, cancelled = 0, partial = 0;
            for (auto& i : p.job->items) { if (i.state == "failed") ++failed; else if (i.state == "cancelled") ++cancelled; else if (i.state == "partial") ++partial; }
            if (p.job->cancel) p.job->state = cancelled || failed || partial ? (failed || partial ? "partial" : "cancelled") : "cancelled";
            else if (partial) p.job->state = "partial";
            else if (failed && failed == p.job->items.size()) p.job->state = "failed";
            else if (failed) p.job->state = "partial";
            else p.job->state = "done";
        }
        p.job->finished = now_ms();
    }
}

void JobQueue::set_item(Job& j, std::size_t idx, const std::string& state, const std::string& error) {
    std::lock_guard<std::mutex> lk(mu_);
    if (idx < j.items.size()) { j.items[idx].state = state; j.items[idx].error = error; }
}

bool JobQueue::cancel(const std::string& id, const std::string& owner, bool admin) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = jobs_.find(id);
    if (it == jobs_.end()) return false;
    if (!admin && it->second->owner != owner) return false;
    if (it->second->state != "queued" && it->second->state != "running") return false;
    it->second->cancel = true;
    for (auto q = queue_.begin(); q != queue_.end(); ++q)
        if (q->job->id == id) { q->job->state = "cancelled"; q->job->finished = now_ms(); queue_.erase(q); break; }
    return true;
}

json JobQueue::get(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = jobs_.find(id);
    return it == jobs_.end() ? json() : it->second->to_json();
}

json JobQueue::list(const std::string& owner, bool all) const {
    std::lock_guard<std::mutex> lk(mu_);
    json out = json::array();
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        auto j = jobs_.find(*it);
        if (j == jobs_.end()) continue;
        if (all || j->second->owner == owner) out.push_back(j->second->to_json());
    }
    return out;
}

bool JobQueue::any_running_on(const std::string& volume_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::string prefix = volume_id + ":";
    for (const auto& [id, j] : jobs_) {
        if (j->state != "running" && j->state != "queued") continue;
        for (const auto& i : j->items)
            if (i.source.rfind(prefix, 0) == 0 || i.destination.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

} // namespace dm
