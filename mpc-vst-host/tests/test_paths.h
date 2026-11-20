// SPDX-License-Identifier: MIT
// Where the build put the mock plug-ins and tools (set by CMake).
#pragma once
#include <string>
#ifndef MPCVST_PLUGIN_DIR
#error "MPCVST_PLUGIN_DIR must be defined by the build"
#endif
#ifndef MPCVST_TOOL_DIR
#error "MPCVST_TOOL_DIR must be defined by the build"
#endif
inline std::string plugin_path(const char* name) { return std::string(MPCVST_PLUGIN_DIR) + "/" + name; }
inline std::string tool_path(const char* name) { return std::string(MPCVST_TOOL_DIR) + "/" + name; }
inline std::string temp_dir() {
    const char* t = std::getenv("MPCVST_TEST_TMP");
    return t ? t : std::string(MPCVST_TOOL_DIR) + "/test-tmp";
}
