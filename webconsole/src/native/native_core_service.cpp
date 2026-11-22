#include "native_core_service.h"
#include "core/path_auth.h"
namespace dm::native {
std::vector<Volume> CoreService::volumes() {
    registry_.refresh();
    std::vector<Volume> out;
    for (const auto& v : registry_.list()) {
        const bool available = v.mounted && v.ownership == Ownership::device;
        out.push_back({v.id, v.name, available, available && v.writable()});
    }
    return out;
}
bool CoreService::list(const std::string& volume, const std::string& relative_dir,
                       std::vector<Item>& out, std::string& error) {
    out.clear();
    registry_.refresh(); // Recheck removable media ownership on every navigation.
    auto v = registry_.get(volume);
    if (!v) { error = "Unknown volume"; return false; }
    if (!v->mounted || v->ownership != Ownership::device) {
        error = "Volume unavailable or owned by USB host"; return false;
    }
    RelPath path;
    const Status parsed = parse_rel_path(relative_dir,path);
    if (!parsed.ok()) { error = parsed.message; return false; }
    Listing listing;
    ListOptions options;
    options.limit = 500;
    const Status st = files_.list(*v,path,options,listing);
    if (!st.ok()) { error = st.message; return false; }
    out.reserve(listing.entries.size());
    for (const auto& e : listing.entries) {
        // Do not expose protected entries to native navigation.
        if (!e.is_protected) out.push_back({e.name,e.type == "dir"});
    }
    return true;
}
} // namespace dm::native
