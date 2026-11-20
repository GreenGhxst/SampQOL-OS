#include "volume_registry.h"
#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>

namespace dm {

const char* volume_kind_name(VolumeKind k) {
    switch (k) { case VolumeKind::internal: return "internal"; case VolumeKind::removable: return "removable";
                 case VolumeKind::usb: return "usb"; case VolumeKind::simulated: return "simulated"; }
    return "unknown";
}
const char* ownership_name(Ownership o) {
    switch (o) { case Ownership::device: return "device"; case Ownership::usb_host: return "usb-host";
                 case Ownership::unmounted: return "unmounted"; case Ownership::unknown: return "unknown"; }
    return "unknown";
}

bool MountEntry::read_only() const {
    std::stringstream ss(options); std::string o;
    while (std::getline(ss, o, ',')) if (o == "ro") return true;
    return false;
}

namespace {

std::string unescape_mount(const std::string& s) {
    // /proc/mounts escapes space, tab, newline and backslash as \040 etc.
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size() && std::isdigit((unsigned char)s[i+1])) {
            out.push_back(char(std::stoi(s.substr(i + 1, 3), nullptr, 8))); i += 3;
        } else out.push_back(s[i]);
    }
    return out;
}

class LinuxProbe : public SystemProbe {
public:
    explicit LinuxProbe(std::string app_medium) : app_medium_(std::move(app_medium)) {}
    std::vector<MountEntry> mounts() override {
        std::vector<MountEntry> v;
        std::ifstream f("/proc/self/mounts");
        std::string line;
        while (std::getline(f, line)) {
            std::stringstream ss(line);
            MountEntry m; std::string dump;
            if (!(ss >> m.device >> m.mount_point >> m.fs_type >> m.options)) continue;
            m.device = unescape_mount(m.device); m.mount_point = unescape_mount(m.mount_point);
            v.push_back(m);
        }
        return v;
    }
    SpaceInfo space(const std::string& path) override {
        struct statvfs st{};
        SpaceInfo s;
        if (::statvfs(path.c_str(), &st) == 0) {
            s.capacity = std::uint64_t(st.f_blocks) * st.f_frsize;
            s.free = std::uint64_t(st.f_bavail) * st.f_frsize;
            s.ok = true;
        }
        return s;
    }
    bool is_directory(const std::string& path) override {
        struct stat st{};
        return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
    std::vector<std::string> usb_exported_disks() override {
        // configfs: /sys/kernel/config/usb_gadget/<g>/functions/mass_storage.*/lun.*/file
        std::vector<std::string> out;
        const char* root = "/sys/kernel/config/usb_gadget";
        DIR* g = ::opendir(root);
        if (!g) return out;
        while (dirent* ge = ::readdir(g)) {
            if (ge->d_name[0] == '.') continue;
            std::string fdir = std::string(root) + "/" + ge->d_name + "/functions";
            DIR* fd = ::opendir(fdir.c_str());
            if (!fd) continue;
            while (dirent* fe = ::readdir(fd)) {
                if (std::strncmp(fe->d_name, "mass_storage.", 13) != 0) continue;
                std::string ldir = fdir + "/" + fe->d_name;
                DIR* ld = ::opendir(ldir.c_str());
                if (!ld) continue;
                while (dirent* le = ::readdir(ld)) {
                    if (std::strncmp(le->d_name, "lun.", 4) != 0) continue;
                    std::ifstream ff(ldir + "/" + le->d_name + "/file");
                    std::string dev; std::getline(ff, dev);
                    if (dev.rfind("/dev/", 0) == 0) out.push_back(dev.substr(5));
                }
                ::closedir(ld);
            }
            ::closedir(fd);
        }
        ::closedir(g);
        return out;
    }
    std::string application_medium_mount() override { return app_medium_; }
private:
    std::string app_medium_;
};

// "mmcblk1p1" -> "mmcblk1", "sda1" -> "sda"
std::string disk_of(const std::string& dev) {
    std::string d = dev.rfind("/dev/", 0) == 0 ? dev.substr(5) : dev;
    if (d.rfind("mmcblk", 0) == 0 || d.rfind("nvme", 0) == 0) {
        auto p = d.rfind('p');
        if (p != std::string::npos && p + 1 < d.size() && std::isdigit((unsigned char)d[p+1])) return d.substr(0, p);
        return d;
    }
    while (!d.empty() && std::isdigit((unsigned char)d.back())) d.pop_back();
    return d;
}

} // namespace

std::unique_ptr<SystemProbe> make_linux_probe(std::string app_medium) {
    return std::make_unique<LinuxProbe>(std::move(app_medium));
}

std::vector<std::string> VolumeInfo::allowed_ops() const {
    std::vector<std::string> ops;
    if (!mounted || ownership != Ownership::device) return ops;
    ops = {"list", "stat", "download", "hash", "search"};
    if (writable()) for (const char* o : {"mkdir", "rename", "copy_in", "move", "upload", "delete", "restore"}) ops.push_back(o);
    return ops;
}

json VolumeInfo::to_json() const {
    return json{
        {"id", id}, {"name", name}, {"kind", volume_kind_name(kind)},
        {"mount_point", mount_point}, {"root", root}, {"device", device}, {"fs_type", fs_type},
        {"mounted", mounted}, {"fs_read_only", fs_read_only}, {"policy_writable", policy_writable},
        {"writable", writable()}, {"ownership", ownership_name(ownership)},
        {"is_application_medium", is_application_medium},
        {"format_candidate", format_candidate},
        {"capacity", capacity}, {"free", free}, {"used", capacity > free ? capacity - free : 0},
        {"protected_paths", protected_paths}, {"allowed_ops", allowed_ops()},
    };
}

VolumeRegistry::VolumeRegistry(std::vector<VolumeRule> rules, std::unique_ptr<SystemProbe> probe)
    : rules_(std::move(rules)), probe_(std::move(probe)) {
    for (auto& r : rules_) if (!valid_id(r.id)) throw std::invalid_argument("invalid volume id in policy: " + r.id);
    refresh();
}

bool VolumeRegistry::valid_id(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    for (char c : id) if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_')) return false;
    return true;
}

void VolumeRegistry::refresh() {
    auto mounts = probe_->mounts();
    auto exported = probe_->usb_exported_disks();
    std::string app_medium = probe_->application_medium_mount();
    std::map<std::string, VolumeInfo> result;
    std::vector<std::string> claimed; // mount points already assigned to a rule

    for (const auto& r : rules_) {
        VolumeInfo v;
        v.id = r.id; v.name = r.name; v.kind = r.kind; v.policy_writable = r.writable;
        v.format_candidate = r.format_candidate; v.protected_paths = r.protected_paths;
        const MountEntry* match = nullptr;
        for (const auto& m : mounts) {
            if (std::find(claimed.begin(), claimed.end(), m.mount_point) != claimed.end()) continue;
            bool ok = false;
            if (!r.mount_point.empty()) ok = (m.mount_point == r.mount_point);
            else if (!r.mount_prefix.empty()) ok = m.mount_point.rfind(r.mount_prefix, 0) == 0 && m.mount_point.size() > r.mount_prefix.size();
            if (ok && !r.fs_types.empty()) ok = std::find(r.fs_types.begin(), r.fs_types.end(), m.fs_type) != r.fs_types.end();
            if (ok) { match = &m; break; }
        }
        if (match) {
            v.mounted = true;
            v.mount_point = match->mount_point;
            v.device = match->device;
            v.fs_type = match->fs_type;
            v.fs_read_only = match->read_only();
            v.root = r.subdir.empty() ? match->mount_point : match->mount_point + "/" + r.subdir;
            if (!probe_->is_directory(v.root)) { v.mounted = false; v.ownership = Ownership::unmounted; }
            else {
                auto sp = probe_->space(v.root);
                v.capacity = sp.capacity; v.free = sp.free;
                std::string disk = disk_of(v.device);
                bool exp = std::find(exported.begin(), exported.end(), disk) != exported.end();
                v.ownership = exp ? Ownership::usb_host : Ownership::device;
                v.is_application_medium = !app_medium.empty() && (match->mount_point == app_medium);
                claimed.push_back(match->mount_point);
            }
        } else {
            v.ownership = Ownership::unmounted;
        }
        result[v.id] = v;
    }
    std::lock_guard<std::mutex> lk(mu_);
    volumes_ = std::move(result);
}

std::vector<VolumeInfo> VolumeRegistry::list() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<VolumeInfo> v;
    for (const auto& r : rules_) { auto it = volumes_.find(r.id); if (it != volumes_.end()) v.push_back(it->second); }
    return v;
}

std::optional<VolumeInfo> VolumeRegistry::get(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = volumes_.find(id);
    if (it == volumes_.end()) return std::nullopt;
    return it->second;
}

std::vector<VolumeRule> VolumeRegistry::default_rules() {
    // Source: melodee-sample shell/project_store.cpp and sample_library.cpp
    // (stock paths /data/UserContent/{Samples,Projects/User}, factory /content)
    // and sample-base tests (card at /media/<id>, exfat). NOT hardware-verified
    // by this project; see docs/AUDIT.md. Internal volumes default to read-only.
    std::vector<VolumeRule> r;
    r.push_back({"sdcard", "microSD card", VolumeKind::removable, "", "/media/", {"exfat", "vfat", "msdos", "fuseblk"}, "",
                 true, true, {"autorun", "mpcqol.conf", ".diskmode"}});
    r.push_back({"internal-samples", "Internal: Samples", VolumeKind::internal, "/data", "", {}, "UserContent/Samples",
                 false, false, {}});
    r.push_back({"internal-projects", "Internal: Projects", VolumeKind::internal, "/data", "", {}, "UserContent/Projects/User",
                 false, false, {}});
    r.push_back({"factory-content", "Factory content (read-only)", VolumeKind::internal, "/content", "", {}, "",
                 false, false, {}});
    return r;
}

std::vector<VolumeRule> VolumeRegistry::rules_from_json(const json& j) {
    std::vector<VolumeRule> out;
    for (const auto& e : j) {
        VolumeRule r;
        r.id = e.at("id").get<std::string>();
        r.name = e.value("name", r.id);
        std::string k = e.value("kind", "internal");
        r.kind = k == "removable" ? VolumeKind::removable : k == "usb" ? VolumeKind::usb : k == "simulated" ? VolumeKind::simulated : VolumeKind::internal;
        r.mount_point = e.value("mount_point", "");
        r.mount_prefix = e.value("mount_prefix", "");
        r.fs_types = e.value("fs_types", std::vector<std::string>{});
        r.subdir = e.value("subdir", "");
        r.writable = e.value("writable", false);
        r.format_candidate = e.value("format_candidate", false);
        r.protected_paths = e.value("protected_paths", std::vector<std::string>{});
        out.push_back(r);
    }
    return out;
}

} // namespace dm
