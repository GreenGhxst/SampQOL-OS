// SPDX-License-Identifier: MIT
// Reads what a candidate plug-in file is without executing any of it: ELF
// header, machine, type, the libraries it needs, its run path and whether
// it exports a VST2 entry point. Every read is bounds-checked: the file is
// untrusted input.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mpcvst {

enum class ElfMachine : uint16_t { Unknown = 0, Arm32 = 40, X86_64 = 62, AArch64 = 183, X86 = 3 };

struct ElfInfo {
    bool is_elf = false;
    bool class64 = false;
    bool little_endian = false;
    bool shared_object = false;        // ET_DYN
    ElfMachine machine = ElfMachine::Unknown;
    uint16_t machine_raw = 0;
    std::vector<std::string> needed;   // DT_NEEDED entries
    std::vector<std::string> runpath;  // DT_RUNPATH / DT_RPATH, split, $ORIGIN expanded
    std::string soname;
    bool exports_vst_entry = false;    // VSTPluginMain, main_plugin or main in .dynsym
    std::string entry_symbol;
    std::string error;                 // why inspection stopped, empty when it finished
    uint64_t file_size = 0;
};

const char* elf_machine_name(ElfMachine machine);

// The machine this host was built for.
ElfMachine host_machine();

// Inspects the file at `path`. Returns false (with info.error) when the file
// cannot be read at all; a non-ELF file returns true with is_elf false.
bool inspect_elf(const std::string& path, ElfInfo& info);

// Where a library this ELF needs would be found: the ELF's run path, the
// extra directories given, LD_LIBRARY_PATH, then the system directories for
// the host machine. Returns the libraries not found.
std::vector<std::string> missing_dependencies(const ElfInfo& info, const std::vector<std::string>& extra_dirs);

} // namespace mpcvst
