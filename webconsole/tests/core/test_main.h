// Tiny self-contained test harness (no external framework needed on the target).
#pragma once
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

struct TestCase { const char* name; std::function<void()> fn; };
std::vector<TestCase>& registry();
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
#define TEST(name) static void name(); static Registrar reg_##name(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " CHECK failed: " #cond); } while (0)
#define CHECK_EQ(a, b) do { if (!((a) == (b))) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " CHECK_EQ failed: " #a " == " #b); } while (0)
#define CHECK_OK(st) do { auto _s = (st); if (!_s.ok()) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " expected ok, got " + dm::err_name(_s.code) + ": " + _s.message); } while (0)
#define CHECK_ERR(st, expected) do { auto _s = (st); if (_s.code != (expected)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " expected " #expected ", got " + dm::err_name(_s.code) + ": " + _s.message); } while (0)

// Temporary directory helper; removed in the destructor.
struct TempDir {
    std::string path;
    TempDir();
    ~TempDir();
    std::string sub(const std::string& rel) const { return path + "/" + rel; }
    void mkdir(const std::string& rel) const;
    void write(const std::string& rel, const std::string& content) const;
    std::string read(const std::string& rel) const;
    bool exists(const std::string& rel) const;
};
