#pragma once

// Portable MD5 (RFC 1321): InstallShield cabinets store one per file, and the cabinet reader checks every file it
// extracts against it (a scratched CD fails instead of converting garbage). Not used for anything else.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace obl::convert {

using Md5Digest = std::array<uint8_t, 16>;

class Md5 {
public:
    void Update(std::span<const uint8_t> data);
    // The digest; the object must not be updated afterwards.
    Md5Digest Digest();

private:
    void Compress(const uint8_t* block);

    uint32_t h_[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    uint8_t block_[64] = {};
    size_t used_ = 0;
    uint64_t total_ = 0;
};

Md5Digest Md5Of(std::span<const uint8_t> data);
std::string HexOf(const Md5Digest& digest);

} // namespace obl::convert
