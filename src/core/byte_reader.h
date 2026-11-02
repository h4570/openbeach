#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace obl {

// The host's byte order, read from a value rather than named, so the check stays a constant expression.
inline constexpr bool kLittleEndianHost = std::bit_cast<std::array<uint8_t, 2>>(uint16_t{1})[0] == 1;

// Reads a little-endian trivially copyable value from a byte buffer without reinterpret_cast or memcpy: data files are
// little-endian whatever the host is (x64, wasm32), and a misaligned offset is fine. The caller checks the bounds
// (`offset + sizeof(T) <= bytes.size()`); ByteFits says whether it holds.
template<class T> constexpr bool ByteFits(std::span<const uint8_t> bytes, size_t offset)
{
    return offset <= bytes.size() && bytes.size() - offset >= sizeof(T);
}

template<class T> constexpr T Read(std::span<const uint8_t> bytes, size_t offset)
{
    static_assert(std::is_trivially_copyable_v<T>, "Read needs a trivially copyable type");
    std::array<uint8_t, sizeof(T)> raw{};
    for (size_t i = 0; i < sizeof(T); ++i) {
        // Byte i of a little-endian value is its i-th least significant byte; a big-endian host takes them reversed.
        const size_t from = kLittleEndianHost ? i : sizeof(T) - 1 - i;
        raw[i] = bytes[offset + from];
    }
    return std::bit_cast<T>(raw);
}

} // namespace obl
