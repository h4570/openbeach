#pragma once

#include "construction_points.h"

#include <cstdint>

// The occupancy grid of a building (docs/specs/construction.md): what construction::ClearFootprint gives back
// to the terrain when a building is demolished. The logic is written against an Env so it compiles without the DLL; the game's grid cells,
// the footprint geometry and the path tile rebuild are the Env's.
namespace obl::construction {

// Cell flags (GridCell::m_flags) the footprint code reads and writes.
constexpr uint32_t kCellBuilding = 0x10; // occupied by the building's body
constexpr uint32_t kCellAroundBuilding = 0x80; // under or next to a building (with 0x10000000: a full path tile)
constexpr uint32_t kCellBuilt = 0x800; // construction finished (FinishBuild)
constexpr uint32_t kCellSurface = 0x0f000000; // the surface chosen by SetSurface under the building
constexpr uint32_t kCellFreeWater = 0x40000000; // free shallow water marker; also "no work point" for the builders
constexpr uint32_t kCellFreeDeep = 0x20000000; // free deep water marker
constexpr uint32_t kCellFreeLand = 0x80000000; // free land marker
constexpr uint32_t kTerrainLand = 3, kTerrainShallow = 4, kTerrainDeep = 8; // the terrain bits the markers are restored from
constexpr int32_t kMaskNone = 0, kMaskMargin = 0x200; // layer 0 values ClearFootprint skips

// What construction::ClearFootprint reads of the building.
struct ClearBuilding {
    bool exists = false;
    int tileX = 0, tileY = 0; // object::Base::GetTilePosition
    uint32_t rotation = 0; // SpriteInstance::m_direction of the building's sprite
    int gridWidth = 0, gridHeight = 0; // BuildingDef::m_GridWidth / m_GridHeight
    const int32_t* terrainLayer = nullptr; // layer 0 of the footprint (footprint +8), 25 cells per row; null = no footprint
};

struct ClearCell {
    uint32_t* flags = nullptr;
    uint16_t* depthOffset = nullptr;
};

struct ApplyBuilding : ClearBuilding {
    const int32_t* workLayer = nullptr;
    const int32_t* depthLayer = nullptr;
    const int32_t* bodyLayer = nullptr;
    const int* surfaces = nullptr;
    uint32_t surfaceCount = 0;
};

// ApplyFootprint takes its origin from the caller, rather than the object's position.
// Env additionally supplies Rand(), RebuildTile(tile, removeNeighbours),
// SetSurface(tile, type), and RemoveLitter(tile). The latter owns the copied object
// list and the cleaning messages; path helpers remain delegated game operations.
template<class Env> void ApplyFootprint(TileXY origin, int phase, int mode, int buildingId, Env& env)
{
    ApplyBuilding building;
    if (!env.Building(buildingId, &building) || !building.exists || !building.terrainLayer) return;
    // Drawing happens even for completion, demolition and the no-op (0,3).
    const int surface = building.surfaceCount ? building.surfaces[static_cast<uint32_t>(env.Rand()) % building.surfaceCount] : 0;
    const bool turned = (building.rotation & 1) != 0;
    const uint32_t width = static_cast<uint32_t>(building.gridWidth) + 2;
    const uint32_t height = static_cast<uint32_t>(building.gridHeight) + 2;
    const uint32_t columns = turned ? height : width, rows = turned ? width : height;
    int columnDirection = 0, rowDirection = 0;
    env.Orient(&origin, rows, columns, &columnDirection, &rowDirection, building.rotation);
    TileXY tile = origin;
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t column = 0; column < columns; ++column) {
            const uint32_t index = turned ? column * kFootprintStride + row : row * kFootprintStride + column;
            const uint32_t terrain = building.terrainLayer[index];
            const uint32_t work = building.workLayer[index];
            const uint32_t body = building.bodyLayer[index];
            const ClearCell cell = env.Cell(tile);
            const uint32_t entry = *cell.flags;
            if (phase == 0) {
                if (mode == 2) {
                    const bool hasSurface = (body & kCellSurface) != 0;
                    if (hasSurface) {
                        if (*cell.flags & 0x10000000) {
                            *cell.flags &= ~0x10000080u;
                            env.RebuildTile(tile, 1);
                        }
                        *cell.flags &= ~kCellSurface;
                        env.RebuildTile(tile, 0);
                        env.RemoveLitter(tile);
                    }
                    if (terrain != 0 && terrain != kMaskMargin) {
                        *cell.flags |= kCellBuilding;
                        if (!(terrain & 0x08000000)) *cell.flags &= ~(kCellFreeLand | kCellFreeWater | kCellFreeDeep);
                    }
                    if (building.depthLayer[index] != 0) *cell.depthOffset = static_cast<uint16_t>(building.depthLayer[index]);
                    *cell.flags |= work & (kCellFreeLand | kCellFreeWater);
                    if ((terrain & kMaskMargin) && (entry & 1)) {
                        *cell.flags |= kCellFreeLand | 0x10000080;
                        env.RebuildTile(tile, 0);
                    }
                    if (work & kCellFreeLand) {
                        if (!(entry & 1) || body == 1)
                            *cell.flags |= kCellAroundBuilding;
                        else {
                            *cell.flags |= 0x10000080;
                            env.RebuildTile(tile, 0);
                        }
                    }
                    if (hasSurface && !(entry & 2)) {
                        env.SetSurface(tile, surface);
                        env.RebuildTile(tile, 0);
                    }
                    if (body == 1) {
                        *cell.flags |= kCellBuilding;
                        *cell.flags &= ~kCellFreeLand;
                    }
                }
            } else if (mode == 2) {
                *cell.flags |= work & kCellBuilt;
            } else if (body == 1) {
                *cell.flags &= ~kCellBuilding;
                if (*cell.flags & kTerrainLand) *cell.flags |= kCellFreeLand;
            }
            tile = env.Neighbour(tile, columnDirection);
        }
        tile = env.Neighbour(origin, rowDirection);
        origin = tile;
    }
}

// Name comparison and list snapshotting belong to Env. Decisions are independent
// of object type or its existing death flag: only the name and job status matter.
template<class Env> void RemoveFootprintLitter(TileXY tile, Env& env)
{
    for (auto* object : env.Objects(tile))
        if (env.IsLitter(object) && env.JobStatus(object) != 5) env.RemoveLitterObject(object);
}

// construction::ClearFootprint (the manager is not used): walks the footprint area `(w+2) x (h+2)` from the building's
// tile in the order Grid::OrientFootprintTraversal gives for its rotation (as CollectWorkPoints; an odd rotation turns the area and reads the
// cell of row r, column c at c*25 + r) and, for each cell whose layer-0 mask is neither 0 nor 0x200, returns the tile to the terrain:
//   - the surface nibble 0x0f000000 is cleared when it was set, then 0x10 (building body) and the depth offset are cleared;
//   - 0x80, 0x40000000 and 0x800 are cleared when they were set; 0x80000000 is cleared always;
//   - RebuildTile(x, y, 0) redraws the tile (the rebuild reads the flags as they are now);
//   - the free marker is put back from the terrain bits the tile had on entry: land (bits 0/1) 0x80000000, else shallow (4) 0x40000000, else
//     deep (8) 0x20000000.
// A building that does not exist or has no footprint writes nothing (the game never gets there).
// Env: bool Building(int id, ClearBuilding* out)
//      void Orient(TileXY* origin, uint32_t rows, uint32_t columns, int* columnDirection, int* rowDirection, uint32_t rotation)
//                                                   Grid::OrientFootprintTraversal
//      TileXY Neighbour(TileXY tile, int direction)  Grid::GetNeighbour
//      ClearCell Cell(TileXY tile)                   the cell of the tile (coordinates wrap modulo the grid)
//      void RebuildTile(TileXY tile)                 RebuildTile(g_PathTiles, x, y, 0)
template<class Env> void ClearFootprint(int buildingId, Env& env)
{
    ClearBuilding building;
    if (!env.Building(buildingId, &building) || !building.exists || !building.terrainLayer) return;
    const uint32_t width = static_cast<uint32_t>(building.gridWidth) + 2;
    const uint32_t height = static_cast<uint32_t>(building.gridHeight) + 2;
    const bool turned = (building.rotation & 1) != 0;
    const uint32_t columns = turned ? height : width;
    const uint32_t rows = turned ? width : height;

    TileXY origin{static_cast<uint32_t>(building.tileX), static_cast<uint32_t>(building.tileY)};
    int columnDirection = 0, rowDirection = 0;
    env.Orient(&origin, rows, columns, &columnDirection, &rowDirection, building.rotation);
    TileXY tile = origin;
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t column = 0; column < columns; ++column) {
            const uint32_t a = turned ? column : row;
            const uint32_t b = turned ? row : column;
            const int32_t mask = building.terrainLayer[b + kFootprintStride * a];
            if (mask != kMaskNone && mask != kMaskMargin) {
                const ClearCell cell = env.Cell(tile);
                const uint32_t entry = *cell.flags;
                // The flags tested are the ones the cell had on entry; each clear applies to the value built so far. Each step stores
                // the cell between the steps, which nothing observes.
                *cell.flags = entry & ~(kCellSurface | kCellBuilding | kCellAroundBuilding | kCellFreeWater | kCellBuilt | kCellFreeLand);
                *cell.depthOffset = 0;
                env.RebuildTile(tile);
                // The rebuild may change the cell's own derived bits (16..23), so the marker is added to what the cell holds now.
                if (entry & kTerrainLand)
                    *cell.flags |= kCellFreeLand;
                else if (entry & kTerrainShallow)
                    *cell.flags |= kCellFreeWater;
                else if (entry & kTerrainDeep)
                    *cell.flags |= kCellFreeDeep;
            }
            tile = env.Neighbour(tile, columnDirection);
        }
        tile = env.Neighbour(origin, rowDirection);
        origin = tile;
    }
}

// What construction::ApplyBuildingToGrid reads of the building.
struct BuildingToGrid : ClearBuilding {
    uint32_t defWidth = 0, defHeight = 0;
    bool hasFootprint = false; // BuildingDef::m_footprint != 0
    int serveOffsetX = 0, serveOffsetY = 0; // BuildingDef::m_ServeOffset (int16 each)
    uint8_t objectGridWidth = 0, objectGridHeight = 0; // Prop::m_gridW / m_gridH (already turned by the rotation)
};

template<class Env> void ApplyBuildingToGrid(int buildingId, Env& env)
{
    BuildingToGrid building;
    if (!env.Building(buildingId, &building) || !building.exists) return;
    const TileXY start{static_cast<uint32_t>(building.tileX), static_cast<uint32_t>(building.tileY)};
    const bool turned = (building.rotation & 1) != 0;
    const uint32_t rows = turned ? building.defHeight : building.defWidth;
    const uint32_t columns = turned ? building.defWidth : building.defHeight;
    if (building.hasFootprint)
        env.ApplyFootprint(start, 0, 2, buildingId);
    else {
        TileXY origin = start;
        for (uint32_t row = rows; row != 0; --row) {
            TileXY tile = origin;
            for (uint32_t column = columns; column != 0; --column) {
                const ClearCell cell = env.Cell(tile);
                *cell.flags |= kCellBuilding;
                *cell.flags &= ~kCellFreeLand;
                if (*cell.flags & 1) *cell.flags |= kCellAroundBuilding;
                tile = env.Neighbour(tile, 0);
            }
            origin = env.Neighbour(origin, 6);
        }
    }
    float serve[2];
    env.FootprintOffsetPoint(start, building.defWidth, building.defHeight, static_cast<float>(static_cast<int16_t>(building.serveOffsetX)),
            static_cast<float>(static_cast<int16_t>(building.serveOffsetY)), building.rotation, serve);
    *env.Cell(env.WorldToTile(serve[0], serve[1])).flags |= kCellFreeLand | kCellAroundBuilding;
    if (!building.hasFootprint) {
        TileXY tile = env.Neighbour(env.Neighbour(start, 2), 4);
        const struct {
            int direction;
            uint32_t steps;
        } sides[] = {{6, rows + 1}, {0, columns + 1}, {2, rows + 1}, {4, columns + 1}};
        for (const auto& side : sides)
            for (uint32_t step = side.steps; step != 0; --step) {
                tile = env.Neighbour(tile, side.direction);
                const ClearCell cell = env.Cell(tile);
                if (*cell.flags & 1) {
                    *cell.flags |= 0x10000080;
                    env.RebuildTile(tile);
                }
            }
    }
    TileXY top = start;
    for (int steps = building.objectGridHeight - 1; steps > 0; --steps) top = env.Neighbour(top, 0);
    for (int steps = building.objectGridWidth - 1; steps > 0; --steps) top = env.Neighbour(top, 6);
    float point[3];
    env.TileToWorld(top, point);
    env.SetTopWorldPosition(point);
}

} // namespace obl::construction
