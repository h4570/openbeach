#pragma once

#include "game/map/grid_state.h"

#include <cstdint>
#include <vector>

// The tile class map of the grid, written by the debug action `SaveGridMap`: one word per tile that classifies its terrain.
// The file goes through an Env: `void Write(const char* path, const uint32_t* words, uint32_t bytes)`.
namespace obl::grid_class_map {

using map::Grid;

inline constexpr uint32_t kSide = 128;
inline constexpr uint32_t kBytes = kSide * kSide * 4;

// The class of a tile from its flags. Flags that match no case repeat the class written for the tile before (the walk is
// column by column).
inline uint32_t TileClass(uint32_t flags, uint32_t previous)
{
    if (flags & 1) return 0x80000001;
    if (flags & 4) return 0x40000004;
    if (flags & 8) return 0x40000004;
    if (flags == 0x10 || flags == 0x20 || flags == 0x8040 || flags == 0x80) return flags;
    if (flags & 2) return 0x80000002;
    if (flags & 0x100) return 0x100;
    return previous;
}

// 128 x 128 words, the outer loop over the first tile coordinate; a first tile that matches nothing has class 0. The file
// is the grid file name taken at construction.
template<class Env> void Save(const Grid& grid, Env& env)
{
    std::vector<uint32_t> words(kSide * kSide);
    uint32_t previous = 0;
    for (uint32_t x = 0; x < kSide; ++x)
        for (uint32_t y = 0; y < kSide; ++y) {
            previous = TileClass(grid.Cell(x, y).m_flags, previous);
            words[x * kSide + y] = previous;
        }
    env.Write(grid.m_gridFileName.c_str(), words.data(), kBytes);
}

} // namespace obl::grid_class_map
