// SPDX-License-Identifier: MIT
// A minimal test harness: CHECK records failures, the summary sets the exit code.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

namespace check {
inline int& failures() { static int f = 0; return f; }
inline int& passes() { static int p = 0; return p; }
inline void record(bool ok, const char* expr, const char* file, int line, const std::string& note = {}) {
    if (ok) { ++passes(); return; }
    ++failures();
    std::fprintf(stderr, "FAIL %s:%d: %s%s%s\n", file, line, expr, note.empty() ? "" : " -- ", note.c_str());
}
inline int summary(const char* name) {
    std::printf("%s: %d checks, %d failed\n", name, passes() + failures(), failures());
    return failures() ? 1 : 0;
}
} // namespace check

#define CHECK(expr) check::record((expr), #expr, __FILE__, __LINE__)
#define CHECK_MSG(expr, msg) check::record((expr), #expr, __FILE__, __LINE__, (msg))
#define REQUIRE(expr) do { if (!(expr)) { check::record(false, #expr, __FILE__, __LINE__, "required; stopping"); return check::summary(__FILE__); } else ++check::passes(); } while (0)
