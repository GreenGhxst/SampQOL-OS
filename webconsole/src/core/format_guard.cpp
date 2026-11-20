#include "format_guard.h"
#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fstream>

namespace dm {

json BlockDevice::to_json() const {
    return json{{"name", name}, {"removable", removable}, {"media", media}, {"size_bytes", size_bytes},
                {"partitions", partitions}, {"is_system", is_system}};
}
json PreflightCheck::to_json() const { return json{{"name", name}, {"pass", pass}, {"detail", detail}}; }

namespace {
std::string disk_of(const std::string& dev) {
    std::string d = dev.rfind("/dev/", 0) == 0 ? dev.substr(5) : dev;
    if (d.rfind("mmcblk", 0) == 0 || d.rfind("nvme", 0) == 0) {
        auto p = d.rfind('p');
        if (p != std::string::npos && p + 1 < d.size() && std::isdigit((unsigned char)d[p + 1])) return d.substr(0, p);
        return d;
    }
    while (!d.empty() && std::isdigit((unsigned char)d.back())) d.pop_back();
    return d;
}

class LinuxBlockInventory : public BlockDeviceInventory {
public:
    std::vector<BlockDevice> list() override {
        std::vector<BlockDevice> out;
        DIR* d = ::opendir("/sys/block");
        if (!d) return out;
        // Mount table to flag system disks.
        std::vector<std::pair<std::string, std::string>> mounts;
        { std::ifstream m("/proc/self/mounts"); std::string dev, mp, rest;
          while (m >> dev >> mp && std::getline(m, rest)) if (dev.rfind("/dev/", 0) == 0) mounts.emplace_back(disk_of(dev), mp); }
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.' || !std::strncmp(e->d_name, "loop", 4) || !std::strncmp(e->d_name, "ram", 3) || !std::strncmp(e->d_name, "zram", 4)) continue;
            BlockDevice b; b.name = e->d_name;
            std::string base = std::string("/sys/block/") + b.name;
            { std::ifstream f(base + "/removable"); int r = 0; f >> r; b.removable = r == 1; }
            { std::ifstream f(base + "/size"); std::uint64_t s = 0; f >> s; b.size_bytes = s * 512; }
            DIR* pd = ::opendir(base.c_str());
            if (pd) { while (dirent* pe = ::readdir(pd)) if (!std::strncmp(pe->d_name, b.name.c_str(), b.name.size()) && std::strcmp(pe->d_name, b.name.c_str())) b.partitions.push_back(pe->d_name); ::closedir(pd); }
            std::sort(b.partitions.begin(), b.partitions.end());
            for (auto& [disk, mp] : mounts)
                if (disk == b.name && (mp == "/" || mp == "/data" || mp == "/content" || mp == "/usr" || mp == "/boot" || mp == "/var" || mp == "/system")) b.is_system = true;
            // "media" (flash_sd) is only known through edisksd, which this
            // process does not query; leave empty so the check fails closed.
            out.push_back(b);
        }
        ::closedir(d);
        return out;
    }
};

class FakeInventory : public BlockDeviceInventory {
public:
    explicit FakeInventory(std::vector<BlockDevice> d) : d_(std::move(d)) {}
    std::vector<BlockDevice> list() override { return d_; }
private:
    std::vector<BlockDevice> d_;
};

class SimulatedExecutor : public FormatExecutor {
public:
    bool is_simulation() const override { return true; }
    Status execute(const FormatRequest& r, json& result) override {
        // Never touches a device. Records what would have been done.
        result = json{{"simulated", true}, {"device", r.device}, {"fs_type", r.fs_type}, {"label", r.label},
                      {"would_run", "privileged-format-helper --volume " + r.volume_id},
                      {"note", "No formatting utility was invoked. Real formatting is disabled in this build."}};
        return Status::good();
    }
};
} // namespace

std::unique_ptr<BlockDeviceInventory> make_linux_block_inventory() { return std::make_unique<LinuxBlockInventory>(); }
std::unique_ptr<BlockDeviceInventory> make_fake_block_inventory(std::vector<BlockDevice> d) { return std::make_unique<FakeInventory>(std::move(d)); }
std::unique_ptr<FormatExecutor> make_simulated_format_executor() { return std::make_unique<SimulatedExecutor>(); }

FormatGuard::FormatGuard(VolumeRegistry& reg, JobQueue& jobs, std::unique_ptr<BlockDeviceInventory> inv,
                         std::unique_ptr<FormatExecutor> exec, std::vector<std::string> supported_fs)
    : reg_(reg), jobs_(jobs), inv_(std::move(inv)), exec_(std::move(exec)), supported_fs_(std::move(supported_fs)) {}

json FormatGuard::devices() {
    json out = json::array();
    for (auto& b : inv_->list()) out.push_back(b.to_json());
    return out;
}

std::vector<PreflightCheck> FormatGuard::run_checks(const std::string& volume_id, std::string& device_out,
                                                    std::uint64_t& size_out, const std::string& fs_type) {
    std::vector<PreflightCheck> c;
    auto add = [&](const std::string& n, bool pass, const std::string& d) { c.push_back({n, d, pass}); };
    reg_.refresh();
    auto v = reg_.get(volume_id);
    if (!v) { add("volume_known", false, "unknown volume id"); return c; }
    add("volume_known", true, v->name);
    add("policy_format_candidate", v->format_candidate, v->format_candidate ? "policy allows offering this volume" : "policy does not allow formatting this volume");
    add("volume_is_removable", v->kind == VolumeKind::removable, std::string("kind: ") + volume_kind_name(v->kind));
    add("not_internal_storage", v->kind != VolumeKind::internal, v->kind == VolumeKind::internal ? "internal storage can never be formatted" : "not internal");

    // Block device discovery
    std::string disk = v->device.empty() ? "" : disk_of(v->device);
    auto devs = inv_->list();
    const BlockDevice* bd = nullptr;
    for (auto& b : devs) if (b.name == disk) bd = &b;
    add("block_device_discovered", bd != nullptr, bd ? ("/dev/" + bd->name) : (disk.empty() ? "volume has no backing device (not mounted?)" : "device " + disk + " not present in inventory"));
    if (bd) {
        device_out = "/dev/" + bd->name; size_out = bd->size_bytes;
        add("device_is_removable_sd", bd->removable && bd->media == "flash_sd",
            bd->removable ? (bd->media == "flash_sd" ? "removable flash_sd media" : "removable but media type unverified (edisksd not consulted)") : "device is not removable");
        add("device_not_system", !bd->is_system, bd->is_system ? "device hosts a system mount" : "no system mounts on device");
        add("device_not_internal_emmc", bd->name != "mmcblk0", bd->name == "mmcblk0" ? "mmcblk0 is the internal eMMC" : "not mmcblk0");
    } else {
        add("device_is_removable_sd", false, "no device"); add("device_not_system", false, "no device"); add("device_not_internal_emmc", false, "no device");
    }
    add("not_application_medium", !v->is_application_medium, v->is_application_medium ? "this card hosts the running application and its data" : "not the application medium");
    add("no_active_jobs", !jobs_.any_running_on(volume_id), jobs_.any_running_on(volume_id) ? "file operations are running on this volume" : "no jobs");
    add("not_exported_to_usb_host", v->ownership != Ownership::usb_host, v->ownership == Ownership::usb_host ? "card is exported as USB mass storage" : "device owns the card");
    add("ownership_known", v->ownership == Ownership::device || v->ownership == Ownership::unmounted, std::string("ownership: ") + ownership_name(v->ownership));
    bool fs_ok = std::find(supported_fs_.begin(), supported_fs_.end(), fs_type) != supported_fs_.end();
    add("filesystem_supported", fs_ok, fs_ok ? fs_type : "unsupported filesystem: " + fs_type);
    add("format_utility_verified", exec_->is_simulation(), exec_->is_simulation() ? "simulation executor (no utility needed)" : "real utility compatibility not verified on hardware");
    add("unmount_remount_verified", exec_->is_simulation(), exec_->is_simulation() ? "simulated" : "edisksd unmount/remount sequence not verified on hardware");
    add("no_format_in_progress", !in_progress_, in_progress_ ? "another format is in progress" : "idle");
    return c;
}

json FormatGuard::preflight(const std::string& volume_id, const std::string& session_id) {
    std::lock_guard<std::mutex> lk(mu_);
    std::string device; std::uint64_t size = 0;
    auto checks = run_checks(volume_id, device, size, supported_fs_.empty() ? "exfat" : supported_fs_[0]);
    bool all = !checks.empty();
    json cj = json::array();
    for (auto& c : checks) { cj.push_back(c.to_json()); all = all && c.pass; }
    json out{{"volume", volume_id}, {"device", device}, {"size_bytes", size}, {"checks", cj}, {"eligible", all},
             {"simulation", exec_->is_simulation()}, {"supported_fs", supported_fs_}};
    auto v = reg_.get(volume_id);
    if (v) out["volume_info"] = json{{"name", v->name}, {"fs_type", v->fs_type}, {"capacity", v->capacity}, {"mount_point", v->mount_point}};
    if (all) {
        // Prune expired tokens, then issue one bound to this exact target.
        std::int64_t now = now_ms();
        for (auto it = tokens_.begin(); it != tokens_.end();) it = it->second.expires < now ? tokens_.erase(it) : std::next(it);
        std::string t = random_token(24);
        tokens_[t] = Token{volume_id, device, session_id, now + 120000};
        out["token"] = t; out["token_expires_in_s"] = 120;
    }
    return out;
}

json FormatGuard::confirm(const std::string& token, const std::string& volume_id, const std::string& device,
                          const std::string& fs_type, const std::string& label, const std::string& session_id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto refuse = [&](const std::string& why, const std::string& code = "forbidden") { return json{{"ok", false}, {"error", code}, {"message", why}}; };
    auto it = tokens_.find(token);
    if (it == tokens_.end()) return refuse("no valid preflight token; run preflight again");
    Token t = it->second;
    tokens_.erase(it);                                  // single use, even on failure
    if (t.expires < now_ms()) return refuse("preflight token expired; run preflight again");
    if (t.session != session_id) return refuse("token belongs to another session");
    if (t.volume != volume_id || t.device != device || device.empty()) return refuse("token does not match the selected volume and device");
    if (label.size() > 11) return refuse("label must be at most 11 characters", "invalid_argument");
    for (unsigned char ch : label) if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch == ' ')) return refuse("label has characters that are not allowed", "invalid_argument");
    if (in_progress_) return refuse("a format is already in progress", "busy");
    std::string dev2; std::uint64_t size = 0;
    auto checks = run_checks(volume_id, dev2, size, fs_type);
    json cj = json::array(); bool all = !checks.empty();
    for (auto& c : checks) { cj.push_back(c.to_json()); all = all && c.pass; }
    if (!all || dev2 != device) return json{{"ok", false}, {"error", "forbidden"}, {"message", "preflight no longer passes"}, {"checks", cj}};
    in_progress_ = true;
    FormatRequest req{volume_id, device, fs_type, label};
    json result;
    Status st = exec_->execute(req, result);
    in_progress_ = false;
    reg_.refresh();
    auto v = reg_.get(volume_id);
    return json{{"ok", st.ok()}, {"error", st.ok() ? "" : err_name(st.code)}, {"message", st.message}, {"result", result},
                {"simulation", exec_->is_simulation()}, {"checks", cj},
                {"remounted", v && v->mounted}, {"filesystem_after", v ? v->fs_type : ""}};
}

} // namespace dm
