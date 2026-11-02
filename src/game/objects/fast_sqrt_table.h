#pragma once
#include <bit>
#include <cmath>
#include <cstdint>

namespace obl::fast_sqrt {
// Keeps the 23 mantissa bits of a float and clears its sign and exponent.
inline constexpr uint32_t kMantissaMask = (1u << 23) - 1;
// Fills the 65536-entry lookup table of the fast square root: the mantissas of sqrt for both exponent parities.
inline void BuildTable(uint32_t* table)
{
    for (uint32_t i = 0; i < 32768; ++i) {
        table[i] = std::bit_cast<uint32_t>(static_cast<float>(std::sqrt(static_cast<double>(std::bit_cast<float>((i | 0x400000u) << 8))))) &
                kMantissaMask;
        table[i + 32768] =
                std::bit_cast<uint32_t>(static_cast<float>(std::sqrt(static_cast<double>(std::bit_cast<float>((i | 0x3f8000u) << 8))))) &
                kMantissaMask;
    }
}
} // namespace obl::fast_sqrt
