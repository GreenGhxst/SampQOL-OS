#pragma once
// Hardware-neutral navigation model. Service owns all filesystem authority.
#include <cstddef>
#include <set>
#include <string>
#include <vector>
namespace dm::native {
struct Item { std::string name; bool directory = false; };
struct Volume { std::string id, label; bool available = false, writable = false; };
struct Service {
    virtual ~Service() = default;
    // Returns a bounded, sorted page. has_more signals further entries.
    virtual bool list(const std::string& volume, const std::string& relative_dir,
                      std::size_t offset, std::size_t limit, std::vector<Item>& out,
                      bool& has_more, std::string& error) = 0;
    virtual std::vector<Volume> volumes() = 0;
};
enum class Screen { volumes, files, error };
enum class Key { up, down, enter, back, select, refresh };
class Controller {
public:
    explicit Controller(Service& service);
    void press(Key key);
    Screen screen() const { return screen_; }
    const std::string& volume_id() const { return volume_; }
    const std::string& directory() const { return dir_; }
    const std::string& error() const { return error_; }
    const std::vector<Volume>& volumes() const { return volumes_; }
    const std::vector<Item>& entries() const { return entries_; }
    std::size_t cursor() const { return cursor_; }
    std::size_t top() const { return top_; }
    bool selected(std::size_t index) const {
        return index < entries_.size() && selection_.count(entries_[index].name) != 0;
    }
    std::size_t selected_count() const { return selection_.size(); }
    bool has_more() const { return has_more_; }
    void set_page_size(std::size_t count);
    static constexpr int width = 320, height = 240;
private:
    static constexpr std::size_t page_batch = 128;
    void reload_volumes(bool preserve = false);
    void reload_files(bool preserve = false);
    bool fetch_page(std::size_t offset);
    void clear_selection();
    void adjust_scroll();
    Service& service_;
    Screen screen_ = Screen::volumes;
    std::vector<Volume> volumes_;
    std::vector<Item> entries_;
    std::string volume_, dir_, error_;
    std::size_t cursor_ = 0, top_ = 0, page_size_ = 7;
    bool has_more_ = false;
    // Name identity is stable within a directory across pagination/sorting.
    std::set<std::string> selection_;
};
} // namespace dm::native
