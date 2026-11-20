// Shared small helpers: JSON alias, status codes, time, random tokens, logging.
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "../../third_party/nlohmann/json.hpp"

namespace dm {

using json = nlohmann::json;

// Error codes shared by core services and the HTTP layer. The HTTP layer maps
// them to status codes; the core never knows about HTTP.
enum class Err {
    none = 0,
    invalid_argument,   // malformed request
    not_found,
    exists,             // destination conflict
    forbidden,          // policy / authorization refused
    read_only,
    busy,
    no_space,
    io,
    unsupported,
    cancelled,
    unavailable,        // storage disappeared, server shutting down
    partial,            // batch finished with some failures
};

const char* err_name(Err e);

struct Status {
    Err code = Err::none;
    std::string message;
    bool ok() const { return code == Err::none; }
    static Status good() { return {}; }
    static Status fail(Err c, std::string m) { return {c, std::move(m)}; }
};

std::int64_t now_ms();
std::string iso8601_now();
std::string random_token(std::size_t bytes = 32);   // lowercase hex, from /dev/urandom
bool constant_time_equal(const std::string& a, const std::string& b);
std::string human_bytes(std::uint64_t n);

enum class LogLevel { debug, info, warn, error };
void log_set_level(LogLevel);
void log(LogLevel, const std::string& msg);
inline void log_info(const std::string& m) { log(LogLevel::info, m); }
inline void log_warn(const std::string& m) { log(LogLevel::warn, m); }
inline void log_error(const std::string& m) { log(LogLevel::error, m); }
inline void log_debug(const std::string& m) { log(LogLevel::debug, m); }

} // namespace dm
