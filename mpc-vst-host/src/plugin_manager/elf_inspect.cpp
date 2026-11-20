// SPDX-License-Identifier: MIT
#include "mpcvst/elf_inspect.h"

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mpcvst {

namespace {

struct View {
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool in(uint64_t off, uint64_t len) const { return off <= size && len <= size - off; }
    uint8_t u8(uint64_t off) const { return data[off]; }
    uint16_t u16(uint64_t off) const { uint16_t v; std::memcpy(&v, data + off, 2); return v; }
    uint32_t u32(uint64_t off) const { uint32_t v; std::memcpy(&v, data + off, 4); return v; }
    uint64_t u64(uint64_t off) const { uint64_t v; std::memcpy(&v, data + off, 8); return v; }
    std::string cstr(uint64_t off, uint64_t limit) const {
        std::string s;
        for (uint64_t i = off; i < size && i < limit && data[i]; ++i) s.push_back(static_cast<char>(data[i]));
        return s;
    }
};

constexpr uint32_t SHT_DYNAMIC = 6, SHT_DYNSYM = 11, SHT_STRTAB = 3;
constexpr int64_t DT_NEEDED = 1, DT_STRTAB = 5, DT_SONAME = 14, DT_RPATH = 15, DT_RUNPATH = 29;
constexpr uint16_t ET_DYN = 3;
constexpr uint8_t STT_FUNC = 2, STT_GNU_IFUNC = 10, STB_GLOBAL = 1, STB_WEAK = 2;

struct Section {
    uint32_t type = 0, link = 0;
    uint64_t offset = 0, size = 0, entsize = 0, addr = 0;
};

std::string dirname_of(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

void split_paths(const std::string& list, const std::string& origin, std::vector<std::string>& out) {
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(':', start);
        if (end == std::string::npos) end = list.size();
        std::string item = list.substr(start, end - start);
        size_t pos;
        while ((pos = item.find("${ORIGIN}")) != std::string::npos) item.replace(pos, 9, origin);
        while ((pos = item.find("$ORIGIN")) != std::string::npos) item.replace(pos, 7, origin);
        if (!item.empty()) out.push_back(item);
        start = end + 1;
    }
}

} // namespace

const char* elf_machine_name(ElfMachine m) {
    switch (m) {
    case ElfMachine::AArch64: return "aarch64";
    case ElfMachine::Arm32: return "arm32";
    case ElfMachine::X86_64: return "x86_64";
    case ElfMachine::X86: return "x86";
    default: return "unknown";
    }
}

ElfMachine host_machine() {
#if defined(__aarch64__)
    return ElfMachine::AArch64;
#elif defined(__x86_64__)
    return ElfMachine::X86_64;
#elif defined(__arm__)
    return ElfMachine::Arm32;
#elif defined(__i386__)
    return ElfMachine::X86;
#else
    return ElfMachine::Unknown;
#endif
}

bool inspect_elf(const std::string& path, ElfInfo& info) {
    info = ElfInfo{};
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        info.error = std::string("cannot open: ") + std::strerror(errno);
        return false;
    }
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        info.error = "not a regular file";
        close(fd);
        return false;
    }
    info.file_size = static_cast<uint64_t>(st.st_size);
    if (st.st_size < 64) {
        close(fd);
        return true; // too small to be an ELF object
    }
    if (static_cast<uint64_t>(st.st_size) > (512ull << 20)) {
        info.error = "file larger than 512 MB";
        close(fd);
        return false;
    }
    void* map = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        info.error = std::string("mmap failed: ") + std::strerror(errno);
        return false;
    }
    View v{static_cast<const uint8_t*>(map), static_cast<size_t>(st.st_size)};

    if (std::memcmp(v.data, "\x7f" "ELF", 4) != 0) {
        munmap(map, v.size);
        return true;
    }
    info.is_elf = true;
    info.class64 = v.u8(4) == 2;
    info.little_endian = v.u8(5) == 1;
    if (!info.little_endian) {
        info.error = "big-endian ELF";
        munmap(map, v.size);
        return true;
    }
    uint16_t type = v.u16(16);
    info.machine_raw = v.u16(18);
    info.shared_object = type == ET_DYN;
    switch (info.machine_raw) {
    case 40: info.machine = ElfMachine::Arm32; break;
    case 62: info.machine = ElfMachine::X86_64; break;
    case 183: info.machine = ElfMachine::AArch64; break;
    case 3: info.machine = ElfMachine::X86; break;
    default: info.machine = ElfMachine::Unknown; break;
    }

    // Section headers (either class; the host only loads its own class, but
    // the report should describe a 32-bit file too).
    uint64_t shoff; uint16_t shentsize, shnum;
    if (info.class64) {
        if (!v.in(0, 64)) { munmap(map, v.size); return true; }
        shoff = v.u64(40); shentsize = v.u16(58); shnum = v.u16(60);
    } else {
        if (!v.in(0, 52)) { munmap(map, v.size); return true; }
        shoff = v.u32(32); shentsize = v.u16(46); shnum = v.u16(48);
    }
    const uint16_t min_entsize = info.class64 ? 64 : 40;
    if (shoff == 0 || shnum == 0 || shentsize < min_entsize || !v.in(shoff, static_cast<uint64_t>(shentsize) * shnum)) {
        info.error = "no readable section headers";
        munmap(map, v.size);
        return true;
    }
    std::vector<Section> sections(shnum);
    for (uint16_t i = 0; i < shnum; ++i) {
        uint64_t o = shoff + static_cast<uint64_t>(i) * shentsize;
        Section& s = sections[i];
        s.type = v.u32(o + 4);
        if (info.class64) {
            s.addr = v.u64(o + 16); s.offset = v.u64(o + 24); s.size = v.u64(o + 32);
            s.link = v.u32(o + 40); s.entsize = v.u64(o + 56);
        } else {
            s.addr = v.u32(o + 12); s.offset = v.u32(o + 16); s.size = v.u32(o + 20);
            s.link = v.u32(o + 24); s.entsize = v.u32(o + 36);
        }
    }
    auto strtab_of = [&](uint32_t link, uint64_t& off, uint64_t& size) {
        if (link >= sections.size() || sections[link].type != SHT_STRTAB) return false;
        off = sections[link].offset; size = sections[link].size;
        return v.in(off, size);
    };

    // .dynamic: DT_NEEDED, DT_SONAME, DT_RUNPATH/DT_RPATH
    const std::string origin = dirname_of(path);
    for (const Section& s : sections) {
        if (s.type != SHT_DYNAMIC) continue;
        uint64_t str_off, str_size;
        if (!strtab_of(s.link, str_off, str_size)) continue;
        const uint64_t ent = info.class64 ? 16 : 8;
        if (!v.in(s.offset, s.size)) continue;
        std::vector<std::string> rpath, runpath;
        for (uint64_t o = s.offset; o + ent <= s.offset + s.size; o += ent) {
            int64_t tag; uint64_t val;
            if (info.class64) { tag = static_cast<int64_t>(v.u64(o)); val = v.u64(o + 8); }
            else { tag = static_cast<int32_t>(v.u32(o)); val = v.u32(o + 4); }
            if (tag == 0) break;
            if (tag != DT_NEEDED && tag != DT_SONAME && tag != DT_RPATH && tag != DT_RUNPATH) continue;
            if (val >= str_size) continue;
            std::string text = v.cstr(str_off + val, str_off + str_size);
            if (tag == DT_NEEDED && info.needed.size() < 256) info.needed.push_back(text);
            else if (tag == DT_SONAME) info.soname = text;
            else if (tag == DT_RUNPATH) split_paths(text, origin, runpath);
            else if (tag == DT_RPATH) split_paths(text, origin, rpath);
        }
        // DT_RUNPATH replaces DT_RPATH when both exist.
        info.runpath = runpath.empty() ? rpath : runpath;
    }

    // .dynsym: an exported function named VSTPluginMain, main_plugin or main.
    for (const Section& s : sections) {
        if (s.type != SHT_DYNSYM) continue;
        uint64_t str_off, str_size;
        if (!strtab_of(s.link, str_off, str_size)) continue;
        const uint64_t ent = info.class64 ? 24 : 16;
        if (s.entsize != ent || !v.in(s.offset, s.size)) continue;
        for (uint64_t o = s.offset; o + ent <= s.offset + s.size; o += ent) {
            uint32_t name = v.u32(o);
            uint8_t sinfo; uint16_t shndx; uint64_t value;
            if (info.class64) { sinfo = v.u8(o + 4); shndx = v.u16(o + 6); value = v.u64(o + 8); }
            else { value = v.u32(o + 4); sinfo = v.u8(o + 12); shndx = v.u16(o + 14); }
            uint8_t stype = sinfo & 0xf, bind = sinfo >> 4;
            if ((stype != STT_FUNC && stype != STT_GNU_IFUNC) || (bind != STB_GLOBAL && bind != STB_WEAK)) continue;
            if (shndx == 0 || value == 0) continue; // undefined: imported, not exported
            if (name >= str_size) continue;
            std::string sym = v.cstr(str_off + name, str_off + str_size);
            if (sym == "VSTPluginMain" || sym == "main_plugin" || sym == "main") {
                // Prefer the modern name when several are exported.
                if (!info.exports_vst_entry || sym == "VSTPluginMain") info.entry_symbol = sym;
                info.exports_vst_entry = true;
            }
        }
    }
    munmap(map, v.size);
    return true;
}

std::vector<std::string> missing_dependencies(const ElfInfo& info, const std::vector<std::string>& extra_dirs) {
    std::vector<std::string> dirs = info.runpath;
    for (const std::string& d : extra_dirs) dirs.push_back(d);
    if (const char* ld = std::getenv("LD_LIBRARY_PATH")) split_paths(ld, ".", dirs);
    static const char* const system_dirs[] = {
        "/lib", "/usr/lib", "/lib64", "/usr/lib64", "/usr/local/lib",
        "/lib/aarch64-linux-gnu", "/usr/lib/aarch64-linux-gnu",
        "/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu",
        "/lib/arm-linux-gnueabihf", "/usr/lib/arm-linux-gnueabihf",
    };
    for (const char* d : system_dirs) dirs.push_back(d);

    std::vector<std::string> missing;
    for (const std::string& lib : info.needed) {
        bool found = false;
        for (const std::string& d : dirs) {
            std::string candidate = d + "/" + lib;
            struct stat st{};
            if (stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                // Right architecture too: a same-named library of another
                // machine does not count.
                ElfInfo dep;
                if (inspect_elf(candidate, dep) && dep.is_elf && dep.machine == info.machine && dep.class64 == info.class64) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) missing.push_back(lib);
    }
    return missing;
}

} // namespace mpcvst
