// Storage Volume Registry.
//
// Discovers mounted filesystems and maps them to policy-defined volumes.
// Clients only ever see the server-side volume id; they never supply paths
// that are trusted as authority. Discovery reads a mounts table (by default
// /proc/self/mounts) through an injectable `SystemProbe` so tests can use a
// fake inventory without touching real devices.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "../util/common.h"

namespace dm {

enum class VolumeKind { internal, removable, usb, simulated };
const char* volume_kind_name(VolumeKind);

// Who currently owns the filesystem. The Web Console only writes when the
// device itself owns it.
enum class Ownership { device, usb_host, unmounted, unknown };
const char* ownership_name(Ownership);

struct MountEntry {
    std::string device;      // /dev/mmcblk1p1
    std::string mount_point; // /media/3265-3932
    std::string fs_type;     // exfat
    std::string options;     // rw,relatime,...
    bool read_only() const;
};

struct SpaceInfo { std::uint64_t capacity = 0, free = 0; bool ok = false; };

// Everything the registry needs from the OS, replaceable in tests.
struct SystemProbe {
    virtual ~SystemProbe() = default;
    virtual std::vector<MountEntry> mounts() = 0;
    virtual SpaceInfo space(const std::string& path) = 0;
    virtual bool is_directory(const std::string& path) = 0;
    // Block devices currently exported to a USB host as mass storage
    // (configfs mass_storage.*/lun.*/file). Whole-disk names like "mmcblk1".
    virtual std::vector<std::string> usb_exported_disks() = 0;
    // The mount point that holds the running application and its data
    // (the SD card with the autorun folder). Empty when unknown.
    virtual std::string application_medium_mount() = 0;
};

std::unique_ptr<SystemProbe> make_linux_probe(std::string application_medium_mount);

// Policy rule describing one volume the console is allowed to expose.
struct VolumeRule {
    std::string id;                 // stable id, e.g. "sdcard"
    std::string name;               // user-visible
    VolumeKind kind = VolumeKind::internal;
    // Matching: either an exact mount point, or a mount-point prefix plus a
    // filesystem list (first matching mount wins).
    std::string mount_point;        // exact, e.g. /data
    std::string mount_prefix;       // e.g. /media/
    std::vector<std::string> fs_types; // optional filter
    std::string subdir;             // expose only this subdirectory of the mount
    bool writable = false;          // policy: writes permitted at all
    bool format_candidate = false;  // policy: may be offered to the format simulator
    std::vector<std::string> protected_paths; // relative prefixes refused for writes/deletes
};

struct VolumeInfo {
    std::string id, name, mount_point, root, device, fs_type;
    VolumeKind kind = VolumeKind::internal;
    bool mounted = false;
    bool fs_read_only = false;     // mount is ro
    bool policy_writable = false;  // rule.writable
    bool is_application_medium = false;
    bool format_candidate = false;
    Ownership ownership = Ownership::unknown;
    std::uint64_t capacity = 0, free = 0;
    std::vector<std::string> protected_paths;

    bool writable() const { return mounted && !fs_read_only && policy_writable && ownership == Ownership::device; }
    std::vector<std::string> allowed_ops() const;
    json to_json() const;
};

class VolumeRegistry {
public:
    VolumeRegistry(std::vector<VolumeRule> rules, std::unique_ptr<SystemProbe> probe);
    void refresh();                                   // re-discover
    std::vector<VolumeInfo> list() const;
    std::optional<VolumeInfo> get(const std::string& id) const;
    // Rules are the only source of ids; unknown ids are refused.
    static bool valid_id(const std::string& id);
    static std::vector<VolumeRule> default_rules();   // MPC Sample layout (unverified on hardware)
    static std::vector<VolumeRule> rules_from_json(const json& j);

private:
    std::vector<VolumeRule> rules_;
    std::unique_ptr<SystemProbe> probe_;
    mutable std::mutex mu_;
    std::map<std::string, VolumeInfo> volumes_;
};

} // namespace dm
