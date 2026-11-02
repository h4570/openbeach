#pragma once

#include "game/ui/map_blocks.h"

#include <cstdint>

namespace obl::map_restore {

inline constexpr int kRectBlockSize = 128;

inline constexpr int kTileRows = 33;
inline constexpr int kTileColumns = 62;

struct PixelRect {
    int32_t x0, y0, x1, y1;
    bool operator==(const PixelRect&) const = default;
};

template<class Env> void EnsureMapped(Env& env, uint32_t x, uint32_t y)
{
    if (!env.Loaded(map_blocks::BlockIndex(env.BlocksPerColumn(), static_cast<int>(x), static_cast<int>(y)))) env.Map(x, y);
    if (!env.Loaded(map_blocks::BlockIndex(env.BlocksPerRow(), static_cast<int>(x), static_cast<int>(y)))) env.Map(x, y);
}

template<class Env> void RestoreRect(Env& env, const PixelRect& rect)
{
    constexpr int bs = kRectBlockSize;
    const int blockX0 = rect.x0 / bs;
    const int blockY0 = rect.y0 / bs;
    const int endX = rect.x1 - blockX0 * bs;
    const int endY = rect.y1 - blockY0 * bs;
    const int startX = rect.x0 % bs;
    uint32_t previousX = 0xffffffffu, previousY = 0xffffffffu;
    uint32_t* view = nullptr;
    const uint16_t* depth = nullptr;
    const uint16_t* colour = nullptr;
    for (int row = rect.y0 % bs; row < endY; ++row) {
        if (startX >= endX) continue;
        const uint32_t y = static_cast<uint32_t>(row / bs + blockY0);
        const int rowInBlock = row % bs;
        for (int column = startX; column < endX; ++column) {
            const uint32_t x = static_cast<uint32_t>(column / bs + blockX0);
            if (x != previousX || y != previousY) {
                EnsureMapped(env, x, y);
                view = env.View(x, y);
                depth = env.Depth(x, y);
                colour = env.Colour(x, y);
            }
            const int pixel = column % bs + rowInBlock * bs;
            view[pixel] = static_cast<uint32_t>(colour[pixel]) | (static_cast<uint32_t>(depth[pixel]) << 16);
            previousX = x;
            previousY = y;
        }
    }
}

struct TileSplit {
    int blockX, blockY, offsetX, offsetY;
    bool operator==(const TileSplit&) const = default;
};
inline TileSplit SplitTilePosition(int worldX, int worldY, int blockSize)
{
    return {worldX / blockSize, worldY / blockSize, worldX % blockSize, worldY % blockSize};
}

struct TileStats {
    uint32_t restored = 0, kept = 0;
};

template<class Env>
void RestoreTileRect(Env& env, uint32_t blockX, uint32_t blockY, int offsetX, int offsetY, const int8_t* mask, TileStats* stats = nullptr)
{
    const int bs = env.BlockSize();
    env.Depth(blockX, blockY);
    env.Colour(blockX, blockY);
    EnsureMapped(env, blockX, blockY);

    int maskPos = 0;
    uint32_t y = blockY;
    uint32_t* view = nullptr;
    const uint16_t* depth = nullptr;
    const uint16_t* colour = nullptr;
    const auto load = [&](uint32_t bx, uint32_t by) {
        EnsureMapped(env, bx, by);
        view = env.View(bx, by);
        colour = env.Colour(bx, by);
        depth = env.Depth(bx, by);
    };
    for (int row = 0, rowParam = offsetY; row < kTileRows; ++row, ++rowParam) {
        int position = offsetX;
        int consumed = 0;
        uint32_t x = blockX;
        if (bs - 1 < rowParam) y = static_cast<uint32_t>(rowParam / bs) + blockY;
        const int rowBase = (rowParam % bs) * bs;
        int pixel = rowBase + offsetX;
        load(x, y);
        do {
            const int8_t code = mask[maskPos++];
            if (code < 0) {
                consumed -= code;
                position -= code;
                if (bs < position) {
                    position %= bs;
                    x = static_cast<uint32_t>((static_cast<int>(x) + 1) % env.BlocksPerRow());
                    load(x, y);
                    pixel = rowBase + position;
                } else {
                    pixel -= code;
                }
            } else if (code != 0) {
                int left = code;
                maskPos += left;
                consumed += left;
                do {
                    if (bs <= position) {
                        x = static_cast<uint32_t>((static_cast<int>(x) + 1) % env.BlocksPerRow());
                        position %= bs;
                        load(x, y);
                        pixel = rowBase + position;
                    }
                    const uint32_t word = view[pixel];
                    const bool unpainted = static_cast<uint16_t>(word >> 16) == depth[pixel];
                    if (unpainted) view[pixel] = colour[pixel] + (word & 0xffff0000u);
                    if (stats) ++(unpainted ? stats->restored : stats->kept);
                    ++pixel;
                    ++position;
                } while (--left != 0);
            }
        } while (consumed < kTileColumns);
    }
}

} // namespace obl::map_restore
