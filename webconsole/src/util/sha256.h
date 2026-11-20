// SHA-256 (FIPS 180-4). Small self-contained implementation so hashing does
// not depend on OpenSSL being available for the target sysroot.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dm {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t len);
    std::array<std::uint8_t, 32> digest();          // finalises; object must not be reused
    std::string hex();                              // finalises and returns lowercase hex
    static std::string hex_of(const std::string& s);

private:
    void block(const std::uint8_t* p);
    std::uint32_t h_[8];
    std::uint8_t buf_[64];
    std::size_t buf_len_ = 0;
    std::uint64_t total_ = 0;
};

std::string to_hex(const std::uint8_t* p, std::size_t n);

} // namespace dm
