#include "native_model.h"
#include <algorithm>
namespace dm::native {
Controller::Controller(Service& s) : service_(s) { reload_volumes(); }
void Controller::clear_selection() { selection_.clear(); }
void Controller::adjust_scroll() {
    if (cursor_ < top_) top_ = cursor_;
    if (cursor_ >= top_ + page_size_) top_ = cursor_ - page_size_ + 1;
}
void Controller::set_page_size(std::size_t n) { page_size_ = std::max<std::size_t>(1,n); adjust_scroll(); }
void Controller::reload_volumes() {
    volumes_ = service_.volumes(); entries_.clear(); volume_.clear(); dir_.clear();
    screen_ = Screen::volumes; cursor_ = top_ = 0; clear_selection(); error_.clear();
}
void Controller::reload_files() {
    entries_.clear(); clear_selection(); cursor_ = top_ = 0; error_.clear();
    if (!service_.list(volume_, dir_, entries_, error_)) {
        entries_.clear(); screen_ = Screen::error;
    } else screen_ = Screen::files;
}
void Controller::press(Key k) {
    if (k == Key::refresh) { if (screen_ == Screen::volumes) reload_volumes(); else if (!volume_.empty()) reload_files(); return; }
    if (screen_ == Screen::error) {
        if (k == Key::back) { reload_volumes(); }
        return;
    }
    auto count = screen_ == Screen::volumes ? volumes_.size() : entries_.size();
    if (k == Key::up) { if (cursor_ > 0) --cursor_; adjust_scroll(); return; }
    if (k == Key::down) { if (cursor_ + 1 < count) ++cursor_; adjust_scroll(); return; }
    if (screen_ == Screen::volumes) {
        if (k == Key::enter && cursor_ < volumes_.size() && volumes_[cursor_].available) {
            volume_ = volumes_[cursor_].id; dir_.clear(); reload_files();
        }
        return;
    }
    if (k == Key::select && cursor_ < entries_.size()) {
        if (!selection_.erase(cursor_)) selection_.insert(cursor_);
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
        // Names are server-provided, but never trust a malicious service.
        if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos) { error_ = "Unsafe filename"; screen_ = Screen::error; return; }
        dir_ += (dir_.empty() ? "" : "/") + name;
        reload_files();
    }
}
} // namespace dm::native
