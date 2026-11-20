#pragma once
#include <sys/stat.h>
#include <sys/statvfs.h>
#include "core/volume_registry.h"

// Fake system probe: mount table points at temp directories; free space can
// be overridden to simulate full volumes; disks can be marked as exported.
struct FakeProbe : dm::SystemProbe {
    std::vector<dm::MountEntry> table;
    std::vector<std::string> exported;
    std::string app_medium;
    std::uint64_t fake_free = ~0ull;      // ~0 = use statvfs
    std::vector<dm::MountEntry> mounts() override { return table; }
    dm::SpaceInfo space(const std::string& p) override {
        struct statvfs st{}; dm::SpaceInfo s;
        if (::statvfs(p.c_str(), &st) == 0) { s.capacity = std::uint64_t(st.f_blocks) * st.f_frsize; s.free = std::uint64_t(st.f_bavail) * st.f_frsize; s.ok = true; }
        if (fake_free != ~0ull) s.free = fake_free;
        return s;
    }
    bool is_directory(const std::string& p) override { struct stat st{}; return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
    std::vector<std::string> usb_exported_disks() override { return exported; }
    std::string application_medium_mount() override { return app_medium; }
};

inline std::vector<dm::VolumeRule> test_rules() {
    std::vector<dm::VolumeRule> r;
    r.push_back({"sdcard", "microSD", dm::VolumeKind::removable, "", "/media/", {"exfat", "vfat", "ext4", "tmpfs", "fake"}, "", false, true, true, {"autorun", "mpcqol.conf", ".diskmode"}});
    r.push_back({"internal", "Internal", dm::VolumeKind::internal, "/data", "", {}, "UserContent/Samples", false, false, false, {}});
    r.push_back({"usb", "USB drive", dm::VolumeKind::usb, "", "/mnt/usb/", {}, "", false, true, false, {}});
    return r;
}
