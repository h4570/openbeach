#pragma once

#include "core/float_to_int.h"
#include "game/ui/map_restore.h"

#include <cstdint>

namespace obl::sprite_bake {

using map_restore::EnsureMapped;
using map_restore::PixelRect;

// The in-block index masks every writer uses: 128 pixels per block row (shift 7, mask 0x7f), whatever the map view's block size says. The remainders keep the sign of
inline constexpr int kBlockEdge = 128;

inline constexpr int kTileMaskStride = 62;
inline constexpr int kTileMaskHeight = 33;

// Sample at pixel centres in doubled coordinates. The two central rows share
// the widest span; the extra bottom row is outside the diamond.
constexpr uint8_t IsoTileMask(int index)
{
    if (index < 0 || index >= kTileMaskStride * kTileMaskHeight) return 0;
    const int x = index % kTileMaskStride;
    const int y = index / kTileMaskStride;
    const int dx = 2 * x - (kTileMaskStride - 1);
    const int dy = 2 * y - 31;
    return (dx < 0 ? -dx : dx) + 2 * (dy < 0 ? -dy : dy) <= 63 ? 1 : 0;
}

// The two frame streams of BakeBuildingDepth: animation 0 (palette indices) and animation 1 (one depth byte per opaque pixel). Only the colour stream's
// control bytes are read; the depth stream is advanced by the same amounts and must have the same run layout. `width` is the current animation's
struct DepthFrames {
    const uint8_t* colour;
    const uint8_t* depth;
    const uint16_t* palette; // Sprite palette 0 (not the instance palette)
    int width;
};

// The frame of BakeBuildingColor / BakeBuildingRectColor: one stream of the current animation whose payload bytes carry a blend mode in the top two bits

struct ColourFrame {
    const uint8_t* colour;
    const uint16_t* palette;
    int width;
    int hotspotY;
    uint16_t halfMask, quarterMask;
};

inline uint32_t DepthWord(uint8_t depthByte, float step, uint32_t depthBase)
{
    const int product = FloatToIntWrap(static_cast<double>(depthByte) * static_cast<double>(step));
    return ((((static_cast<uint32_t>(product) & 0xffffu) + (depthBase & 0xffffu)) << 16) & 0xfffeffffu) + 0x10000u;
}

template<class Env>
void BakeDepth(Env& env, const PixelRect& rect, int blockX, int blockY, int offsetX, int offsetY, float depthStep, uint32_t depthBase,
        const DepthFrames& frames)
{
    const uint8_t* colour = frames.colour;
    const uint8_t* depth = frames.depth;
    const int width = frames.width;
    const auto skipRow = [&] {
        int column = 0;
        if (width <= 0) return;
        do {
            const int8_t code = static_cast<int8_t>(*colour++);
            ++depth;
            if (code > 0) {
                column += code;
                colour += code;
                depth += code;
            } else {
                column -= code;
            }
        } while (column < width);
    };
    for (int skipped = rect.y0; skipped > 0; --skipped) skipRow();
    if (rect.y0 >= rect.y1) return;
    int rowPosition = offsetY + rect.y0;
    for (int row = rect.y0; row < rect.y1; ++row, ++rowPosition) {
        int column = 0;
        if (width <= 0) continue;
        do {
            const int8_t code = static_cast<int8_t>(*colour++);
            ++depth;
            if (code > 0) {
                int position = column + offsetX;
                for (int left = code; left > 0; --left, ++column, ++position, ++colour, ++depth) {
                    if (column < rect.x0 || column >= rect.x1) continue;
                    const int blockXi = position / kBlockEdge + blockX;
                    const int blockYi = rowPosition / kBlockEdge + blockY;
                    EnsureMapped(env, static_cast<uint32_t>(blockXi), static_cast<uint32_t>(blockYi));
                    uint32_t* view = env.View(static_cast<uint32_t>(blockXi), static_cast<uint32_t>(blockYi));
                    const uint32_t word = DepthWord(*depth, depthStep, depthBase);
                    uint32_t& target = view[(rowPosition % kBlockEdge) * kBlockEdge + position % kBlockEdge];
                    if ((target & 0xffff0000u) <= word) target = frames.palette[*colour] + word;
                }
            } else {
                column -= code;
            }
        } while (column < width);
    }
}

// The three blend modes of the payload's top bits (0x00 is the plain palette colour). `source` is the palette colour, `dest` the colour already in the
// block; the masks keep the shifted channels from bleeding into their neighbours. 0x80 is a 50 % mix, 0x40 weighs the source 75 %, 0xc0 the destination.
inline uint16_t BlendColour(unsigned mode, uint16_t source, uint16_t dest, uint16_t halfMask, uint16_t quarterMask)
{
    const uint32_t half = halfMask >> 1, quarter = quarterMask >> 2;
    switch (mode) {
    case 0x80: return static_cast<uint16_t>((((dest >> 1) & half) + ((source >> 1) & half)) & 0xffffu);
    case 0x40: return static_cast<uint16_t>((((source >> 1) & half) + ((dest >> 2) & quarter) + ((source >> 2) & quarter)) & 0xffffu);
    default: // 0xc0
        return static_cast<uint16_t>((((dest >> 1) & half) + ((dest >> 2) & quarter) + ((source >> 2) & quarter)) & 0xffffu);
    }
}

// The body BakeBuildingColor and BakeBuildingRectColor share. The depth of a row ramps from `((depth & 0xffff) + hotspotY * 0xfff8) << 16`: before each row
// (skipped ones too) it grows by 0x100000 when the row counter, starting at the hotspot, is non-negative and falls by 0x80000 otherwise. A pixel is painted
// where the block's depth word is not above the row's; mode 0 stores the palette colour with the row's depth, the blends keep the block's own depth.
// `relativeToRect` (Color) measures the pixel position from the rectangle's corner, RectColor from the frame origin. `gate(column, row)` is RectColor's
// tile-mask test, true to store.
template<class Env, class Gate>
void BakeColourBody(Env& env, const PixelRect& rect, int blockX, int blockY, int offsetX, int offsetY, uint32_t depth,
        const ColourFrame& frame, bool relativeToRect, Gate&& gate)
{
    const uint8_t* colour = frame.colour;
    const int width = frame.width;
    const int blockSize = env.BlockSize();
    uint32_t rowDepth = ((depth & 0xffffu) + static_cast<uint32_t>(frame.hotspotY) * 0xfff8u) << 16;
    int rowCounter = frame.hotspotY;
    const auto step = [&] {
        rowDepth = rowCounter < 0 ? rowDepth - 0x80000u : rowDepth + 0x100000u;
        ++rowCounter;
    };
    for (int skipped = rect.y0; skipped > 0; --skipped) {
        step();
        int column = 0;
        if (width <= 0) continue;
        do {
            const int8_t code = static_cast<int8_t>(*colour++);
            if (code > 0) {
                column += code;
                colour += code;
            } else {
                column -= code;
            }
        } while (column < width);
    }
    for (int row = rect.y0; row < rect.y1; ++row) {
        step();
        int column = 0;
        if (width <= 0) continue;
        do {
            const int8_t code = static_cast<int8_t>(*colour++);
            if (code > 0) {
                for (int left = code; left > 0; --left, ++column, ++colour) {
                    if (column < rect.x0 || column >= rect.x1) continue;
                    const int positionX = (relativeToRect ? column - rect.x0 : column) + offsetX;
                    const int positionY = (relativeToRect ? row - rect.y0 : row) + offsetY;
                    const int blockXi = positionX / blockSize + blockX;
                    const int blockYi = positionY / blockSize + blockY;
                    EnsureMapped(env, static_cast<uint32_t>(blockXi), static_cast<uint32_t>(blockYi));
                    uint32_t* view = env.View(static_cast<uint32_t>(blockXi), static_cast<uint32_t>(blockYi));
                    uint32_t& target = view[(positionY % kBlockEdge) * kBlockEdge + positionX % kBlockEdge];
                    const uint32_t existing = target & 0xffff0000u;
                    if (existing > rowDepth) continue;
                    const unsigned mode = *colour & 0xc0u;
                    const uint16_t source = frame.palette[*colour - mode];
                    const uint16_t dest = static_cast<uint16_t>(target);
                    uint16_t result;
                    uint32_t newDepth;
                    if (mode == 0) {
                        result = source;
                        newDepth = rowDepth;
                    } else {
                        result = BlendColour(mode, source, dest, frame.halfMask, frame.quarterMask);
                        newDepth = existing;
                    }
                    if (gate(column, row)) target = result + newDepth;
                }
            } else {
                column -= code;
            }
        } while (column < width);
    }
}

template<class Env>
void BakeColour(Env& env, const PixelRect& rect, int blockX, int blockY, int offsetX, int offsetY, uint32_t depth, const ColourFrame& frame)
{
    BakeColourBody(env, rect, blockX, blockY, offsetX, offsetY, depth, frame, true, [](int, int) { return true; });
}

template<class Env>
void BakeRectColour(Env& env, const PixelRect& rect, int blockX, int blockY, int offsetX, int offsetY, uint32_t depth, int maskX, int maskY,
        const ColourFrame& frame)
{
    BakeColourBody(env, rect, blockX, blockY, offsetX, offsetY, depth, frame, false,
            [&](int column, int row) { return env.Mask(((row - rect.y0) + maskY) * kTileMaskStride - rect.x0 + column + maskX) != 0; });
}

// What BakeBuilding / BakeBuildingRect hand to their writer: the clip rectangle moved into the frame's own coordinates (the frame origin's map position
// subtracted from all four edges) and that position split at the block size (signed division and remainder).
struct WriterPosition {
    PixelRect rect;
    int blockX, blockY, offsetX, offsetY;
    bool operator==(const WriterPosition&) const = default;
};
inline WriterPosition ToWriterPosition(PixelRect rect, int mapX, int mapY, int blockSize)
{
    rect.x0 -= mapX;
    rect.y0 -= mapY;
    rect.x1 -= mapX;
    rect.y1 -= mapY;
    return {rect, mapX / blockSize, mapY / blockSize, mapX % blockSize, mapY % blockSize};
}

// BakeBuildingRect's depth for the rectangle writer: the point's depth plus 8, made odd.
inline uint32_t RectDepth(uint32_t pointDepth)
{
    const uint32_t depth = pointDepth + 8;
    return (depth & 1u) == 0 ? depth + 1 : depth;
}

struct FrameClip {
    PixelRect rect;
    int x, y;
    bool clipped;
};

// Masked clips at the fixed 4096 map edge; Ghost only moves negative origins to zero.
// The right/bottom values remain frame coordinates, including when the left/top are clipped.
inline FrameClip ClipFrame(int x, int y, int width, int height, bool masked)
{
    FrameClip out{{0, 0, width, height}, x, y, false};
    if (out.x < 0) {
        out.rect.x0 = -out.x;
        out.x = 0;
        out.clipped = true;
    }
    if (masked && out.x + out.rect.x1 > 4096) {
        out.rect.x1 = 4096 - out.x;
        out.clipped = true;
    }
    if (masked && out.y + out.rect.y1 > 4096) {
        out.rect.y1 = 4096 - out.y;
        out.clipped = true;
    }
    if (out.y < 0) {
        out.rect.y0 = -out.y;
        out.y = 0;
        out.clipped = true;
    }
    return out;
}

template<class Env>
void BakeMaskedColour(Env& env, int height, int blockX, int blockY, int offsetX, int offsetY, uint32_t depth, const ColourFrame& frame)
{
    BakeColourBody(env, {0, 0, frame.width, height}, blockX, blockY, offsetX, offsetY, depth, frame, false, [](int, int) { return true; });
}

// GhostColor's rectangle only limits rows. It deliberately ignores both X edges and
// starts each painted row at offsetX. Block walking wraps X and maps even empty rows
// and transparent runs; arithmetic lookup per opaque pixel would lose these effects.
template<class Env>
void BakeGhost(Env& env, const PixelRect& rect, int blockX, int blockY, int offsetX, int offsetY, float step, uint32_t depthBase,
        const DepthFrames& frames)
{
    const uint8_t* colour = frames.colour;
    for (int row = 0; row < rect.y0; ++row) {
        for (int column = 0; column < frames.width;) {
            const int code = static_cast<int8_t>(*colour++);
            column += code > 0 ? code : -code;
            if (code > 0) colour += code;
        }
    }
    const int bs = env.BlockSize();
    int by = blockY;
    for (int row = rect.y0; row < rect.y1; ++row) {
        const int y = row - rect.y0 + offsetY;
        if (y >= bs) by = y / bs + blockY;
        const int rowIndex = (y % bs) * bs;
        int bx = blockX, x = offsetX, index = rowIndex + x;
        const auto map = [&] {
            EnsureMapped(env, static_cast<uint32_t>(bx), static_cast<uint32_t>(by));
            return env.View(static_cast<uint32_t>(bx), static_cast<uint32_t>(by));
        };
        uint32_t* view = map();
        for (int column = 0; column < frames.width;) {
            const int code = static_cast<int8_t>(*colour++);
            if (code < 0) {
                column -= code;
                x -= code;
                if (x < bs) {
                    index -= code;
                } else {
                    x %= bs;
                    bx = (bx + 1) % env.BlocksPerRow();
                    view = map();
                    index = rowIndex + x;
                }
            } else if (code > 0) {
                column += code;
                for (int n = 0; n < code; ++n, ++colour, ++x, ++index) {
                    if (x >= bs) {
                        x %= bs;
                        bx = (bx + 1) % env.BlocksPerRow();
                        view = map();
                        index = rowIndex + x;
                    }
                    const uint32_t word = DepthWord(frames.depth[colour - frames.colour], step, depthBase);
                    if ((view[index] & 0xffff0000u) <= word) view[index] = frames.palette[*colour] + word;
                }
            }
        }
    }
}

// The frame one ground tile block draws: animation 0's stream under the current frame and direction, palette 0, the size of the *current* animation
struct GroundFrame {
    const uint8_t* colour;
    const uint16_t* palette;
    int width, height;
    uint16_t halfMask, quarterMask;
};

template<class Env>
void BakeGroundTile(Env& env, int blockX, int blockY, int offsetX, int offsetY, uint32_t depth, const GroundFrame& frame)
{
    const int bs = env.BlockSize();
    const uint8_t* stream = frame.colour;
    int by = blockY;
    uint32_t rowDepth = 0;
    for (int row = 0, rowPosition = offsetY; row < frame.height; ++row, ++rowPosition, rowDepth += 0x10u) {
        if (bs - 1 < rowPosition) by = rowPosition / bs + blockY;
        const int rowIndex = (rowPosition % bs) * bs;
        int bx = blockX, x = offsetX, index = rowIndex + x;
        const auto map = [&] {
            EnsureMapped(env, static_cast<uint32_t>(bx), static_cast<uint32_t>(by));
            return env.View(static_cast<uint32_t>(bx), static_cast<uint32_t>(by));
        };
        uint32_t* view = map();
        const uint32_t limit = ((depth & 0xffffu) + rowDepth) << 16;
        for (int column = 0; column < frame.width;) {
            const int code = static_cast<int8_t>(*stream++);
            if (code < 0) {
                column -= code;
                x -= code;
                if (x > bs) {
                    x %= bs;
                    bx = (bx + 1) % env.BlocksPerRow();
                    view = map();
                    index = rowIndex + x;
                } else {
                    index -= code;
                }
            } else if (code > 0) {
                column += code;
                for (int n = 0; n < code; ++n, ++stream, ++x, ++index) {
                    if (x >= bs) {
                        x %= bs;
                        bx = (bx + 1) % env.BlocksPerRow();
                        view = map();
                        index = rowIndex + x;
                    }
                    const uint32_t existing = view[index] & 0xffff0000u;
                    if (existing > limit) continue;
                    const unsigned mode = *stream & 0xc0u;
                    const uint16_t source = frame.palette[*stream - mode];
                    const uint16_t result = mode == 0
                            ? source
                            : BlendColour(mode, source, static_cast<uint16_t>(view[index]), frame.halfMask, frame.quarterMask);
                    view[index] = result + existing;
                }
            }
        }
    }
}

} // namespace obl::sprite_bake
