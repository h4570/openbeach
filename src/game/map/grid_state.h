#pragma once

#include "game/map/grid_cell.h"
#include "game/map/grid_math.h"
#include "formats/map_pack.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace obl::tables {
struct BuildingDef;
struct SceneryDef;
} // namespace obl::tables

// The terrain grid of the world (docs/architecture/map.md): the cells, the height map, the beach and sea areas and
// the ferry route. The tile geometry its queries answer is in grid_math.h.
namespace obl::map {

// A polygon vertex of an area, in tiles.
struct TilePoint {
    uint32_t x = 0;
    uint32_t y = 0;
};

struct Area {
    std::vector<TilePoint> m_points;
    int m_minX = 0;
    int m_minY = 0;
    int m_maxX = 0;
    int m_maxY = 0;
    bool m_shut = false; // open while false (the save labels the value `Closed`); swimming in a sea area needs it open

    uint32_t NumPoints() const { return static_cast<uint32_t>(m_points.size()); }
};

// The boat route of a map script has a handful of points.
inline constexpr int kMaxBoatPoints = 10;

// A point of the boat route (`BoatPoint x y [Unload|Destroy];` of the map script).
struct BoatPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// `BoatEntry`/`BoatExit x y n dx dy;`: where the ferry appears / leaves.
struct BoatEnd {
    float m_pos[3] = {};
    float m_dir[2] = {};
    int m_number = 0;
};

// The ferry's route: a bounded list of points, the indices of the Unload and Destroy points and the two ends.
struct BoatRoute {
    std::array<BoatPoint, kMaxBoatPoints> m_points{};
    int m_numPoints = 0;
    int m_destroyPointIndex = 0; // index of the `BoatPoint ... Destroy` point (flag 2)
    int m_unloadPointIndex = 0; // index of the `BoatPoint ... Unload` point (flag 1)
    BoatEnd m_entry;
    BoatEnd m_exit;
};

struct Grid {
    int m_tileSize = 0; // world units per tile edge (64)
    uint32_t m_width = 0; // tiles (128)
    uint32_t m_height = 0;
    std::vector<GridCell> m_cells; // m_width * m_height cells, row-major
    std::optional<map_pack::HeightSamples> m_heightMap; // the converted height map of the map, none = flat terrain
    float m_heightScale = 0.0f; // terrain height = interpolated sample * scale
    std::vector<Area> m_beachAreas; // `Area Beach` in script order; the save and the scripts address them by index
    std::vector<Area> m_seaAreas; // `Area Sea`
    std::string m_gridFileName; // the grid file name taken by the constructor
    BoatRoute m_boat;
    int m_islandSize = 0; // tiles with a land or water bit (flags & 3), counted while loading the grid file

    const map_pack::HeightSamples* HeightMap() const { return m_heightMap ? &*m_heightMap : nullptr; }
    uint32_t NumCells() const { return m_width * m_height; }
    // Both coordinates wrap modulo the dimensions.
    uint32_t TileIndex(uint32_t tileX, uint32_t tileY) const { return (tileY % m_height) * m_width + tileX % m_width; }
    // Shallow const, as when the cells were a pointer: callers holding a const grid still write cell flags (the route endpoints).
    GridCell& Cell(uint32_t tileX, uint32_t tileY) const { return const_cast<GridCell&>(m_cells[TileIndex(tileX, tileY)]); }

    // The area `index` of a list, null for an index out of range.
    static const Area* AreaAt(const std::vector<Area>& areas, int index)
    {
        return index >= 0 && static_cast<size_t>(index) < areas.size() ? &areas[static_cast<size_t>(index)] : nullptr;
    }
    static Area* AreaAt(std::vector<Area>& areas, int index)
    {
        return index >= 0 && static_cast<size_t>(index) < areas.size() ? &areas[static_cast<size_t>(index)] : nullptr;
    }

    // ---- tile queries (grid_queries.cpp) ----
    grid_math::TileXY GetNeighbour(uint32_t x, uint32_t y, int direction) const;
    void GetNeighbourCells(uint32_t x, uint32_t y, GridCell** out) const;
    bool AreTilesAdjacent(uint32_t x, uint32_t y, uint32_t otherX, uint32_t otherY) const;
    // The z of a world point is not an input of the tile lookup.
    grid_math::TileXY WorldPointToTile(float x, float y) const;
    grid_math::WorldPoint TileToWorldPoint(uint32_t x, uint32_t y, int kind) const;
    grid_math::WorldPoint TileToWorldOrigin(uint32_t x, uint32_t y) const;
    grid_math::WorldPoint TileToWorldCenter(uint32_t x, uint32_t y) const; // z is zero
    grid_math::PathPoint TileToPathPoint(uint32_t x, uint32_t y) const;
    GridCell* GetCellAtWorldPoint(float x, float y) const;

    // ---- footprint and placement queries (grid_queries.cpp) ----
    // These read the dimensions of the world's grid; only the orientation also reads this grid's own size. Orient moves `origin`
    // to the footprint's first tile and writes the two directions only when the walk touched them.
    void OrientFootprintTraversal(uint32_t* origin, int rows, int columns, int* columnDirection, int* rowDirection, int rotation) const;
    bool IsFootprintCrossingMapBoundary(uint32_t x, uint32_t y, int width, int height) const;
    bool CanPlaceSceneryTile(uint32_t x, uint32_t y, const tables::SceneryDef& definition) const;
    bool CanPlaceBuildingEdge(uint32_t x, uint32_t y, const tables::BuildingDef& definition) const;
    // A rotation above 3 has no defined point and stops the game.
    grid_math::PathPoint GetFootprintOffsetPoint(
            uint32_t x, uint32_t y, int width, int height, float offsetX, float offsetY, uint32_t rotation) const;

    // ---- areas and the boat route (grid_areas.cpp) ----
    // `kind` as in grid_areas.h; an invalid kind or index stops the game.
    bool IsAreaOpen(int kind, int area) const;
    void SetAreaOpen(int kind, int area);
    void SetAreaClosed(int kind, int area);
    // The index of the area that holds the world point.
    int GetBeachAreaOfPoint(float x, float y, float z) const;
    int GetSeaAreaOfPoint(float x, float y, float z) const;
    // Built by the map script while the world is created.
    void AddBeachArea(Area area);
    void AddSeaArea(Area area);
    void AddBoatPoint(float x, float y, float z, int32_t flags);
    void SetBoatEntry(float x, float y, float z, int32_t number, float dx, float dy);
    void SetBoatExit(float x, float y, float z, int32_t number, float dx, float dy);

    // ---- screen rectangles and the tile class map (grid_screen_rect.cpp) ----
    // The tile rectangle that a screen rectangle covers, for the path tile redraw.
    void ScreenRectToTileRect(const int32_t* rect, int32_t* outX, int32_t* outY, int32_t* outWidth, int32_t* outHeight) const;
    // Writes the tile class map of the grid (the debug action `SaveGridMap`).
    void SaveTileClassMap() const;
};

} // namespace obl::map
