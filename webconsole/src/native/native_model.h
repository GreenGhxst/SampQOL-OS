#pragma once
// Hardware-agnostic 320x240 DISKMODE navigation state. No stock input hooks.
// Service implementations MUST enforce volume/path policy before returning data.
#include <cstddef>
#include <set>
#include <string>
#include <vector>
namespace dm::native {
struct Item { std::string name; bool directory = false; };
struct Volume { std::string id, label; bool available = false, writable = false; };
struct Service {
    virtual ~Service() = default;
    virtual std::vector<Volume> volumes() = 0;
    virtual bool list(const std::string& volume, const std::string& relative_dir,
                      std::vector<Item>& out, std::string& error) = 0;
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
    bool selected(std::size_t index) const { return selection_.count(index) != 0; }
    void set_page_size(std::size_t count);
    // Fixed 320x240 content area is rendered by a separate hardware adapter.
    static constexpr int width = 320, height = 240;
private:
    void reload_volumes();
    void reload_files();
    void clear_selection();
    void adjust_scroll();
    Service& service_;
    Screen screen_ = Screen::volumes;
    std::vector<Volume> volumes_;
    std::vector<Item> entries_;
    std::string volume_, dir_, error_;
    std::size_t cursor_ = 0, top_ = 0, page_size_ = 7;
    std::set<std::size_t> selection_;
};
} // namespace dm::native
