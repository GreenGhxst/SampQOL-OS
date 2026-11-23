#include "native_core_service.h"
#include "core/path_auth.h"
#include <algorithm>
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
                       std::size_t offset, std::size_t limit, std::vector<Item>& out,
                       bool& has_more, std::string& error) {
    out.clear(); has_more = false; error.clear();
    if (!limit || limit > 256 || offset > 20000) { error = "Invalid page request"; return false; }
    registry_.refresh();
    const auto v = registry_.get(volume);
    if (!v) { error = "Unknown volume"; return false; }
    if (!v->mounted || v->ownership != Ownership::device) {
        error = "Volume unavailable or owned by USB host"; return false;
    }
    RelPath path;
    const Status parsed = parse_rel_path(relative_dir,path);
    if (!parsed.ok()) { error = parsed.message; return false; }
    // Iterate pages from the existing authorized FileOps API, counting only
    // visible entries. Never expose a protected item, including across pages.
    std::size_t source_offset = 0, visible_offset = 0;
    while (source_offset < 20000) {
        Listing listing;
        ListOptions options;
        options.offset = source_offset;
        options.limit = 2000;
        const Status st = files_.list(*v,path,options,listing);
        if (!st.ok()) { out.clear(); error = st.message; return false; }
        if (listing.entries.empty()) break;
        for (const auto& e : listing.entries) {
            if (e.is_protected) continue;
            if (visible_offset >= offset) {
                if (out.size() < limit) out.push_back({e.name,e.type == "dir"});
                else { has_more = true; return true; }
            }
            ++visible_offset;
        }
        source_offset += listing.entries.size();
        if (source_offset >= listing.total) break;
    }
    // Underlying FileOps caps each directory listing at 20,000 entries.
    // Do not pretend the tail was fully indexed when that bound was reached.
    return true;
}
} // namespace dm::native
