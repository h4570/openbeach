#pragma once

// The handheld panel's frame preparation, without SDL (frame_share.cpp): which rows changed since the frame last shown, and the game's
// 16 bpp pixels as RGBA. The Mali's texture upload is the costly part of showing a frame, and the game hands over the same picture
// frame after frame in its menus and changes only parts of it in play, so only the bands of rows that changed are converted and
// uploaded.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace obl::platform {

inline constexpr int kFrameBandRows = 16; // rows compared and uploaded together

// Rows [begin, end) of a frame.
struct RowRun {
    int begin, end;
};

// Whether `bytes` bytes at `a` and `b` differ. A loop the compiler vectorizes rather than memcmp: the Zig build carries its own plain
// memcmp instead of glibc's, which made comparing a whole frame cost 4 ms on the RG40XXV.
inline bool Differs(const uint8_t* a, const uint8_t* b, size_t bytes)
{
    uint64_t difference = 0;
    size_t i = 0;
    for (; i + 8 <= bytes; i += 8) {
        uint64_t x, y;
        std::memcpy(&x, a + i, 8); // fixed-size copies become plain loads
        std::memcpy(&y, b + i, 8);
        difference |= x ^ y;
    }
    for (; i < bytes; ++i) difference |= static_cast<uint64_t>(a[i] ^ b[i]);
    return difference != 0;
}

// The runs of rows whose bands differ between `previous` and `current` (`height` rows of `pitch` bytes); adjacent changed bands form
// one run, so that each run is one upload. With `all` the whole frame is one run (a new texture has nothing to keep).
inline std::vector<RowRun> ChangedRows(const uint8_t* previous, const uint8_t* current, size_t pitch, int height, bool all)
{
    std::vector<RowRun> runs;
    for (int y = 0; y < height; y += kFrameBandRows) {
        const int end = y + kFrameBandRows < height ? y + kFrameBandRows : height;
        const size_t offset = static_cast<size_t>(y) * pitch, bytes = static_cast<size_t>(end - y) * pitch;
        if (!all && !Differs(previous + offset, current + offset, bytes)) continue;
        if (!runs.empty() && runs.back().end == y)
            runs.back().end = end;
        else
            runs.push_back({y, end});
    }
    return runs;
}

// One row of RGB565 (or XRGB1555) pixels as ABGR8888: the bytes R, G, B, A that GLES takes as they are. SDL2's GLES renderer has no
// 16 bpp texture format, and its own conversion of the whole frame used a core; this loop vectorizes.
inline void RowToRgba(const uint16_t* in, uint32_t* out, int width, bool rgb555)
{
    if (rgb555) {
        for (int x = 0; x < width; ++x) {
            const uint32_t p = in[x], r = (p >> 10) & 31u, g = (p >> 5) & 31u, b = p & 31u;
            out[x] = 0xff000000u | (((b << 3) | (b >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((r << 3) | (r >> 2));
        }
    } else {
        for (int x = 0; x < width; ++x) {
            const uint32_t p = in[x], r = p >> 11, g = (p >> 5) & 63u, b = p & 31u;
            out[x] = 0xff000000u | (((b << 3) | (b >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((r << 3) | (r >> 2));
        }
    }
}

} // namespace obl::platform
