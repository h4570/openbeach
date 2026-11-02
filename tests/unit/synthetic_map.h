#pragma once

// Synthetic map sources (importer/src/writers/map_decode.cpp): the terrain grid, the RefPack-compressed BIZ blocks, the
// water animation and the height map, our own bytes. convert_map_decode_test.cpp checks the decoders on them,
// convert_import_test.cpp imports a whole synthetic copy.

#include "synthetic_exe.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace obl::test {

// A grid file stores column-major words (x outer, y inner).
inline std::vector<uint8_t> Grid(uint32_t (*flag)(size_t x, size_t y))
{
    std::vector<uint8_t> b(128 * 128 * 4);
    for (size_t x = 0; x < 128; ++x)
        for (size_t y = 0; y < 128; ++y) PutU32(b, (x * 128 + y) * 4, flag(x, y));
    return b;
}

// One RefPack stream decoding to 65536 bytes: four literal bytes, then backreferences of distance 4 repeating them, then an
// end command.
inline std::vector<uint8_t> RepeatedBlock(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    std::vector<uint8_t> out = {0x10, 0xfb, 0x01, 0x00, 0x00}; // header, size 65536 (big-endian 24-bit)
    out.insert(out.end(), {0xe0, a, b, c, d}); // 4 literals
    size_t produced = 4;
    while (produced < 65536) {
        // 0xc0-0xdf: count = ((a & 0xc) << 6) + d + 5 (up to 1028), distance = (b << 8) + c + 1.
        const size_t count = std::min<size_t>(1028, 65536 - produced);
        if (count < 5) {
            // Short tail: the 2-byte form copies 3..10 bytes.
            out.insert(out.end(), {static_cast<uint8_t>((count - 3) << 2), 0x03});
        } else {
            const size_t value = count - 5;
            out.insert(out.end(), {static_cast<uint8_t>(0xc0 | ((value >> 8) << 2)), 0x00, 0x03, static_cast<uint8_t>(value & 0xff)});
        }
        produced += count;
    }
    out.push_back(0xfc); // end, no literals
    return out;
}

inline std::vector<uint8_t> WaterFile(int8_t run)
{
    // 15 frames sharing one run list that fills 65536 pixels with runs of |run|.
    const size_t runs = 65536 / static_cast<size_t>(run < 0 ? -run : run);
    std::vector<uint8_t> b(124 + runs);
    PutU32(b, 0, static_cast<uint32_t>(runs));
    for (size_t frame = 0; frame < 15; ++frame) {
        PutU32(b, 4 + frame * 4, 0);
        PutU32(b, 64 + frame * 4, static_cast<uint32_t>(runs));
    }
    for (size_t i = 0; i < runs; ++i) b[124 + i] = static_cast<uint8_t>(i % 2 ? -run : run);
    return b;
}

inline std::vector<uint8_t> HeightTga(int width, int height)
{
    // Uncompressed color-mapped 8-bit TGA, 256 x 24-bit palette, bottom-up rows.
    std::vector<uint8_t> b(18 + 256 * 3 + size_t(width) * height);
    b[1] = 1;
    b[2] = 1;
    b[5] = 0;
    b[6] = 1; // 256 palette entries
    b[7] = 24;
    b[12] = static_cast<uint8_t>(width);
    b[13] = static_cast<uint8_t>(width >> 8);
    b[14] = static_cast<uint8_t>(height);
    b[15] = static_cast<uint8_t>(height >> 8);
    b[16] = 8;
    for (int i = 0; i < 256; ++i) b[18 + i * 3] = b[18 + i * 3 + 1] = b[18 + i * 3 + 2] = static_cast<uint8_t>(i);
    for (size_t i = 0; i < size_t(width) * height; ++i) b[18 + 256 * 3 + i] = static_cast<uint8_t>(i);
    return b;
}

// A BIZ file: the 32 x 32 offset and size tables, every block pointing at the same RefPack stream.
inline std::vector<uint8_t> Biz(const std::vector<uint8_t>& block)
{
    std::vector<uint8_t> b(8192);
    for (size_t i = 0; i < 1024; ++i) {
        PutU32(b, i * 4, 8192);
        PutU32(b, 4096 + i * 4, static_cast<uint32_t>(block.size()));
    }
    b.insert(b.end(), block.begin(), block.end());
    return b;
}

} // namespace obl::test
