#include "native_model.h"
#include <algorithm>
#include <utility>
namespace dm::native {
Controller::Controller(Service& s) : service_(s) { reload_volumes(); }
void Controller::clear_selection() { selection_.clear(); }
void Controller::adjust_scroll() {
    if (cursor_ < top_) top_ = cursor_;
    if (cursor_ >= top_ + page_size_) top_ = cursor_ - page_size_ + 1;
}
void Controller::set_page_size(std::size_t n) { page_size_ = std::max<std::size_t>(1,n); adjust_scroll(); }
void Controller::reload_volumes(bool preserve) {
    std::string prior = (preserve && cursor_ < volumes_.size()) ? volumes_[cursor_].id : "";
    volumes_ = service_.volumes(); entries_.clear(); volume_.clear(); dir_.clear();
    screen_ = Screen::volumes; cursor_ = top_ = 0; clear_selection(); error_.clear(); has_more_ = false;
    if (preserve) for (std::size_t i=0; i<volumes_.size(); ++i)
        if (volumes_[i].id == prior) { cursor_ = i; break; }
    adjust_scroll();
}
bool Controller::fetch_page(std::size_t offset) {
    std::vector<Item> page;
    bool more = false;
    std::string fault;
    if (!service_.list(volume_, dir_, offset, page_batch, page, more, fault)) {
        error_ = fault.empty() ? "Unable to list directory" : fault;
        screen_ = Screen::error; return false;
    }
    // Never trust a service's pagination metadata. Prevent no-progress loops.
    if (page.empty() && more) { error_ = "Invalid directory page"; screen_ = Screen::error; return false; }
    entries_.insert(entries_.end(),page.begin(),page.end());
    has_more_ = more; return true;
}
void Controller::reload_files(bool preserve) {
    std::string prior = preserve && cursor_ < entries_.size() ? entries_[cursor_].name : "";
    std::set<std::string> prior_selection = preserve ? selection_ : std::set<std::string>{};
    entries_.clear(); cursor_ = top_ = 0; error_.clear(); has_more_ = false; clear_selection();
    if (!fetch_page(0)) return;
    if (preserve) {
        // Walk remaining pages only when needed to restore selection/cursor.
        while (has_more_) {
            bool pending = !prior.empty();
            if (pending) for (const auto& e : entries_) if (e.name == prior) { pending = false; break; }
            for (const auto& key : prior_selection) {
                bool found = false;
                for (const auto& e : entries_) if (e.name == key) { found = true; break; }
                if (!found) pending = true;
            }
            if (!pending) break;
            if (!fetch_page(entries_.size())) return;
        }
        for (std::size_t i=0;i<entries_.size();++i) {
            if (entries_[i].name == prior) cursor_ = i;
            if (prior_selection.count(entries_[i].name)) selection_.insert(entries_[i].name);
        }
    }
    screen_ = Screen::files; adjust_scroll();
}
void Controller::press(Key k) {
    if (k == Key::refresh) {
        if (screen_ == Screen::volumes) reload_volumes(true);
        else if (!volume_.empty()) reload_files(true);
        return;
    }
    if (screen_ == Screen::error) { if (k == Key::back) reload_volumes(); return; }
    if (k == Key::up) { if (cursor_ > 0) --cursor_; adjust_scroll(); return; }
    if (k == Key::down) {
        std::size_t count = screen_ == Screen::volumes ? volumes_.size() : entries_.size();
        if (screen_ == Screen::files && cursor_ + 1 >= count && has_more_) {
            if (!fetch_page(entries_.size())) return;
            count = entries_.size();
        }
        if (cursor_ + 1 < count) ++cursor_;
        adjust_scroll(); return;
    }
    if (screen_ == Screen::volumes) {
        if (k == Key::enter && cursor_ < volumes_.size() && volumes_[cursor_].available) {
            volume_ = volumes_[cursor_].id; dir_.clear(); reload_files();
        }
        return;
    }
    if (k == Key::select && cursor_ < entries_.size()) {
        const auto& key = entries_[cursor_].name;
        if (!selection_.erase(key)) selection_.insert(key);
        return;
    }
    if (k == Key::back) {
        if (dir_.empty()) { reload_volumes(); return; }
        const auto pos = dir_.rfind('/');
        dir_ = pos == std::string::npos ? "" : dir_.substr(0,pos);
        reload_files(); return;
    }
    if (k == Key::enter && cursor_ < entries_.size() && entries_[cursor_].directory) {
        const std::string& name = entries_[cursor_].name;
        if (name.empty() || name == "." || name == ".." || name.size()>255 ||
            name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
            std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32 || c==127;})) {
            error_ = "Unsafe filename"; screen_ = Screen::error; return;
        }
        dir_ += (dir_.empty() ? "" : "/") + name; reload_files();
    }
}
} // namespace dm::native
