#include "common.h"
#include <atomic>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include "sha256.h"

namespace dm {

const char* err_name(Err e) {
    switch (e) {
        case Err::none: return "ok";
        case Err::invalid_argument: return "invalid_argument";
        case Err::not_found: return "not_found";
        case Err::exists: return "exists";
        case Err::forbidden: return "forbidden";
        case Err::read_only: return "read_only";
        case Err::busy: return "busy";
        case Err::no_space: return "no_space";
        case Err::io: return "io";
        case Err::unsupported: return "unsupported";
        case Err::cancelled: return "cancelled";
        case Err::unavailable: return "unavailable";
        case Err::partial: return "partial";
    }
    return "unknown";
}

std::int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string iso8601_now() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string random_token(std::size_t bytes) {
    std::vector<std::uint8_t> buf(bytes);
    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    std::size_t got = 0;
    if (fd >= 0) {
        while (got < bytes) {
            ssize_t n = ::read(fd, buf.data() + got, bytes - got);
            if (n <= 0) break;
            got += std::size_t(n);
        }
        ::close(fd);
    }
    if (got < bytes) {
        // /dev/urandom should never fail; refuse to hand out weak tokens.
        log_error("random_token: /dev/urandom unavailable");
        std::abort();
    }
    return to_hex(buf.data(), buf.size());
}

bool constant_time_equal(const std::string& a, const std::string& b) {
    // Compare fixed-length digests so the length does not leak either.
    std::string ha = Sha256::hex_of(a), hb = Sha256::hex_of(b);
    unsigned char diff = 0;
    for (std::size_t i = 0; i < ha.size(); ++i) diff |= (unsigned char)(ha[i] ^ hb[i]);
    return diff == 0 && a.size() == b.size();
}

std::string human_bytes(std::uint64_t n) {
    const char* u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = double(n); int i = 0;
    while (v >= 1024 && i < 4) { v /= 1024; ++i; }
    char buf[32];
    std::snprintf(buf, sizeof buf, i == 0 ? "%.0f %s" : "%.1f %s", v, u[i]);
    return buf;
}

static std::atomic<int> g_level{int(LogLevel::info)};
static std::mutex g_log_mutex;
void log_set_level(LogLevel l) { g_level = int(l); }
void log(LogLevel l, const std::string& msg) {
    if (int(l) < g_level.load()) return;
    const char* tag = l == LogLevel::debug ? "DEBUG" : l == LogLevel::info ? "INFO" : l == LogLevel::warn ? "WARN" : "ERROR";
    std::lock_guard<std::mutex> lk(g_log_mutex);
    std::fprintf(stderr, "%s [%s] %s\n", iso8601_now().c_str(), tag, msg.c_str());
}

} // namespace dm
