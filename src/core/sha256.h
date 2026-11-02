#pragma once

// Portable SHA-256 (FIPS 180-4): the importer's source hashes in pack.json and the pack identity of native saves; the
// unit tests also hash run/BeachLife.exe with it.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace obl::sha256 {

class Hasher {
public:
    void Update(std::span<const uint8_t> data);
    // Lowercase hex digest; the object must not be updated afterwards.
    std::string HexDigest();

private:
    void Compress(const uint8_t* block);

    uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t block_[64] = {};
    size_t used_ = 0;
    uint64_t total_ = 0;
};

std::string Hex(std::span<const uint8_t> data);
// Hash of a file's contents, or nullopt when it cannot be read.
std::optional<std::string> OfFile(const std::filesystem::path& path);

} // namespace obl::sha256
