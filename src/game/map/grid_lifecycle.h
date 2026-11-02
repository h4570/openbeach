#pragma once

#include "core/char16.h"
#include "game/map/grid_state.h"

#include <cstdint>
#include <cstring>
#include <filesystem>

// Construction and destruction of the world's terrain grid (docs/architecture/map.md). The game side
// (the world grid pointer, the pack files, the map view) goes through `Env`; the order of the writes and of the calls is
// fixed, because the file and map view calls have effects of their own (the fog table, the height map).
namespace obl::grid_lifecycle {

using map::Grid;
using map::GridCell;

// The scenario grid file is read as 128 x 128 words whatever the dimensions passed in, and the map view has a 32 x 32 table
// of background blocks, each answered by a 5 x 5 block of cells.
constexpr uint32_t kFileSide = 0x80;
constexpr uint32_t kFogSide = 0x20;
constexpr uint32_t kFogScreenShift = 7;
constexpr uint32_t kFogCellSpan = 5;
constexpr uint32_t kFogTerrainMask = 0xc;
constexpr uint32_t kFileWaterOrLand = 3;
constexpr uint32_t kFileOverrideBit = 0x100;
constexpr uint32_t kBorderFlags = 0x20;

// Env contract (all game side):
//   void SetGlobalGrid(Grid*)                        the world's grid (the map view calls below read it, so it is set first)
//   bool HasHeightMap(const char* gridPath)          the map of the grid has a converted height map (map_pack.h)
//   void RememberHeightMapName(const char* path)     keeps the name of the height map for the save
//   bool LoadHeightMap(const char* gridPath, map_pack::HeightSamples& out)   the converted height map (the grid path names the map)
//   const uint32_t* ReadGridFile(const char* path)   the terrain words of the map
//   void FreeGridFile(const uint32_t*)               releases them
//   void ScreenToWorld(const int32_t screen[2], float world[3])   the map view's screen-to-world projection
//   void WorldToTile(const float world[3], int32_t tile[2])       the map view's world-to-tile projection
//   uint32_t FogStride()                             background blocks per column of the map view
//   void MarkFog(uint32_t index)                     flags background block `index` as showing terrain
template<class Env>
void ConstructGrid(Grid& grid, Env& env, uint32_t width, uint32_t height, int32_t tileSize, const char* heightMapPath, float heightScale,
        const char* gridFilePath)
{
    env.SetGlobalGrid(&grid);
    grid.m_width = width;
    const uint32_t count = width * height;
    grid.m_height = height;
    grid.m_tileSize = tileSize;
    grid.m_cells.assign(count, GridCell{});
    grid.m_beachAreas.clear();
    grid.m_seaAreas.clear();
    grid.m_boat.m_numPoints = 0;
    grid.m_gridFileName = gridFilePath;

    // The height map is looked up by the grid's map (the pack keeps it under the map id), the name passed in is only remembered.
    if (env.HasHeightMap(gridFilePath)) {
        env.RememberHeightMapName(heightMapPath);
        grid.m_heightMap.emplace();
        if (!env.LoadHeightMap(gridFilePath, *grid.m_heightMap)) grid.m_heightMap.reset();
        grid.m_heightScale = heightScale;
    } else {
        grid.m_heightMap.reset();
        grid.m_heightScale = 0.0f;
    }

    const uint32_t* const file = env.ReadGridFile(gridFilePath);
    const uint32_t* word = file;
    grid.m_islandSize = 0;
    for (uint32_t outer = 0; outer < kFileSide; ++outer)
        for (uint32_t inner = 0; inner < kFileSide; ++inner) {
            GridCell& cell = grid.Cell(outer, inner);
            cell.m_flags = *word;
            if (*word & kFileOverrideBit) cell.m_flags = kFileOverrideBit;
            if (*word & kFileWaterOrLand) ++grid.m_islandSize;
            // The last word is not stepped over, so the file's own end is never read past.
            if (outer != kFileSide - 1 || inner != kFileSide - 1) ++word;
        }

    // A background block of the map view is flagged when a cell with a terrain bit (0xc) lies in the 5 x 5 block of cells from the tile under its corner.
    for (uint32_t column = 0; column < kFogSide; ++column)
        for (uint32_t row = 0; row < kFogSide; ++row) {
            const int32_t screen[2] = {static_cast<int32_t>(column << kFogScreenShift), static_cast<int32_t>(row << kFogScreenShift)};
            float world[3];
            env.ScreenToWorld(screen, world);
            int32_t tile[2];
            env.WorldToTile(world, tile);
            bool found = false;
            for (uint32_t dy = 0; dy < kFogCellSpan; ++dy)
                for (uint32_t dx = 0; dx < kFogCellSpan; ++dx)
                    if (grid.Cell(static_cast<uint32_t>(tile[0]) + dx, static_cast<uint32_t>(tile[1]) + dy).m_flags & kFogTerrainMask)
                        found = true;
            if (found) env.MarkFog(env.FogStride() * row + column);
        }

    // The frame of the map is a wall: every border cell is overwritten (not merged) with the "outside" bit.
    grid.Cell(0, 0).m_flags = kBorderFlags;
    for (uint32_t u = 0; u < kFileSide; ++u) {
        grid.Cell(0, u).m_flags = kBorderFlags;
        grid.Cell(kFileSide - 1, u).m_flags = kBorderFlags;
        grid.Cell(u, 0).m_flags = kBorderFlags;
        grid.Cell(u, kFileSide - 1).m_flags = kBorderFlags;
    }
    env.FreeGridFile(file);
}

// Env contract:
//   void SetGlobalGrid(Grid*)
// The area lists and the height map are emptied (the sea list, then the beach list), then the cells.
template<class Env> void DestructGrid(Grid& grid, Env& env)
{
    grid.m_seaAreas.clear();
    grid.m_beachAreas.clear();
    grid.m_heightMap.reset();
    grid.m_cells.clear();
    env.SetGlobalGrid(nullptr);
}

} // namespace obl::grid_lifecycle

// The world's grid in the running game (grid_lifecycle.cpp): made with `new` and kept as the world's grid (game::GetGrid);
// the terrain and the height map come from the selected pack under the game directory (map_pack.h).
namespace obl::map {

Grid* CreateGrid(uint32_t width, uint32_t height, int32_t tileSize, const char* heightMapPath, float heightScale, const char* gridFilePath);
void DestroyGrid(Grid* grid);

// The directory of the selected pack, where the converted terrain, height maps and water sheets are; set once at startup.
void SetPackDirectory(const Text16& gameDirectory);
const std::filesystem::path& PackDirectory();

} // namespace obl::map
