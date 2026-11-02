#include "game/map/grid_state.h"

#include "core/call_counter.h"
#include "core/fatal.h"
#include "game/game/game_accessors.h"
#include "game/map/grid_cell.h"
#include "game/map/grid_math.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>

namespace obl::map {
namespace {

// Calls on the default path (`call-report <name>`): the neighbour walk, the world point of a tile and every other query.
test::CallCounter g_neighbourCalls("map.grid-neighbour");
test::CallCounter g_worldPointCalls("map.tile-to-world");
test::CallCounter g_queryCalls("map.grid-queries");

grid_math::GridDims DimsOf(const Grid& grid)
{
    return {grid.m_tileSize, grid.m_width, grid.m_height};
}

// The footprint queries measure against the world's grid, whichever grid they are asked on.
grid_math::GridDims WorldDims()
{
    const Grid* grid = game::GetGrid();
    return grid ? DimsOf(*grid) : grid_math::GridDims{0, 1, 1};
}

} // namespace

grid_math::TileXY Grid::GetNeighbour(uint32_t x, uint32_t y, int direction) const
{
    g_neighbourCalls.Count();
    return grid_math::Neighbour(m_width, m_height, x, y, direction);
}

void Grid::GetNeighbourCells(uint32_t x, uint32_t y, GridCell** out) const
{
    g_queryCalls.Count();
    uint32_t indexes[8];
    grid_math::NeighbourIndices(m_width, m_height, x, y, indexes);
    for (int i = 0; i < 8; ++i) out[i] = const_cast<GridCell*>(m_cells.data()) + indexes[i];
}

bool Grid::AreTilesAdjacent(uint32_t x, uint32_t y, uint32_t otherX, uint32_t otherY) const
{
    g_queryCalls.Count();
    return grid_math::AreTilesAdjacent(m_width, m_height, x, y, otherX, otherY);
}

void Grid::OrientFootprintTraversal(uint32_t* origin, int rows, int columns, int* columnDirection, int* rowDirection, int rotation) const
{
    g_queryCalls.Count();
    grid_math::TileXY tile{origin[0], origin[1]};
    bool touched = false;
    const grid_math::TraversalDirections directions =
            grid_math::OrientFootprintTraversal(DimsOf(*this), WorldDims(), tile, rows, columns, static_cast<uint32_t>(rotation), touched);
    origin[0] = tile.x;
    origin[1] = tile.y;
    if (touched) {
        *columnDirection = directions.columnDirection;
        *rowDirection = directions.rowDirection;
    }
}

bool Grid::IsFootprintCrossingMapBoundary(uint32_t x, uint32_t y, int width, int height) const
{
    g_queryCalls.Count();
    return grid_math::FootprintCrossesMapBoundary(WorldDims(), x, y, width, height);
}

bool Grid::CanPlaceSceneryTile(uint32_t x, uint32_t y, const tables::SceneryDef& definition) const
{
    g_queryCalls.Count();
    return grid_math::CanPlaceSceneryTile(Cell(x, y).m_flags, definition.m_BuildOn);
}

bool Grid::CanPlaceBuildingEdge(uint32_t x, uint32_t y, const tables::BuildingDef& definition) const
{
    g_queryCalls.Count();
    return grid_math::CanPlaceBuildingEdge(Cell(x, y).m_flags, definition.m_BuildOn);
}

grid_math::PathPoint Grid::GetFootprintOffsetPoint(
        uint32_t x, uint32_t y, int width, int height, float offsetX, float offsetY, uint32_t rotation) const
{
    grid_math::PathPoint point{};
    if (!grid_math::FootprintOffsetPoint(WorldDims(), x, y, width, height, offsetX, offsetY, rotation, point))
        fatal::UnhandledInput("Grid::GetFootprintOffsetPoint", "a rotation above 3");
    g_queryCalls.Count();
    return point;
}

grid_math::TileXY Grid::WorldPointToTile(float x, float y) const
{
    g_queryCalls.Count();
    return grid_math::WorldPointToTile(m_tileSize, m_width, m_height, x, y);
}

grid_math::WorldPoint Grid::TileToWorldPoint(uint32_t x, uint32_t y, int kind) const
{
    g_worldPointCalls.Count();
    return grid_math::TileToWorldPoint(m_tileSize, x, y, kind);
}

// The origin is the tile's point of kind 0.
grid_math::WorldPoint Grid::TileToWorldOrigin(uint32_t x, uint32_t y) const
{
    g_queryCalls.Count();
    return grid_math::TileToWorldPoint(m_tileSize, x, y, 0);
}

grid_math::WorldPoint Grid::TileToWorldCenter(uint32_t x, uint32_t y) const
{
    g_queryCalls.Count();
    const grid_math::PathPoint point = grid_math::TileToWorldCenter(m_tileSize, x, y);
    return {point.x, point.y, 0.0f};
}

grid_math::PathPoint Grid::TileToPathPoint(uint32_t x, uint32_t y) const
{
    g_queryCalls.Count();
    return grid_math::TileToPathPoint(m_tileSize, x, y);
}

GridCell* Grid::GetCellAtWorldPoint(float x, float y) const
{
    g_queryCalls.Count();
    const grid_math::TileXY tile = grid_math::WorldPointToTile(m_tileSize, m_width, m_height, x, y);
    return &Cell(tile.x, tile.y);
}

} // namespace obl::map
