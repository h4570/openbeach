#pragma once

#include "game/map/grid_math.h"
#include "game/map/grid_state.h"

#include <cmath>
#include <cstdint>
#include <vector>

// The beach and sea areas and the boat route of the terrain grid (docs/architecture/map.md);
// the save records are in grid_save.h. A query without a defined answer (an index out of range, a polygon without
// points, a tile size below 2) reports `false`, and the Grid members stop the game there.
namespace obl::grid_areas {

using map::Area;
using map::Grid;

// The `kind` of an area query: 0 beach area, 1 sea area, 2 both areas with the same index. Any other kind does nothing / is
// closed.
constexpr int kKindBeach = 0, kKindSea = 1, kKindBoth = 2;

// An area is open while its `m_shut` is false. `valid` is false for an index out of range.
inline bool IsAreaOpen(const Grid& grid, int kind, int area, bool& valid)
{
    valid = true;
    if (kind == kKindBeach || kind == kKindSea) {
        const Area* node = Grid::AreaAt(kind == kKindBeach ? grid.m_beachAreas : grid.m_seaAreas, area);
        valid = node != nullptr;
        return node && !node->m_shut;
    }
    if (kind == kKindBoth) {
        const Area* beach = Grid::AreaAt(grid.m_beachAreas, area);
        const Area* sea = Grid::AreaAt(grid.m_seaAreas, area);
        valid = beach && sea;
        return beach && sea && !beach->m_shut && !sea->m_shut;
    }
    return false;
}

// Writes `m_shut` of the area(s). Returns false (nothing written) for an index out of range; an unknown kind writes nothing
// and answers true.
inline bool SetAreaShut(Grid& grid, int kind, int area, bool shut)
{
    if (kind == kKindBeach || kind == kKindSea) {
        Area* node = Grid::AreaAt(kind == kKindBeach ? grid.m_beachAreas : grid.m_seaAreas, area);
        if (!node) return false;
        node->m_shut = shut;
        return true;
    }
    if (kind == kKindBoth) {
        Area* beach = Grid::AreaAt(grid.m_beachAreas, area);
        Area* sea = Grid::AreaAt(grid.m_seaAreas, area);
        if (!beach || !sea) return false;
        beach->m_shut = shut;
        sea->m_shut = shut;
    }
    return true;
}
inline bool SetAreaOpen(Grid& grid, int kind, int area)
{
    return SetAreaShut(grid, kind, area, false);
}
inline bool SetAreaClosed(Grid& grid, int kind, int area)
{
    return SetAreaShut(grid, kind, area, true);
}

// The integers after the point list of an `Area` command: x0 y0 x1 y1 x2 y2 x3 y3 and a ninth that nothing reads.
inline constexpr int kAreaIntegers = 9;

// The area of an `Area` command: the point list copied, the rectangle from the first and the last corner point, open.
inline Area MakeArea(const uint32_t* points, uint32_t numPoints, const int32_t integers[kAreaIntegers])
{
    Area area;
    area.m_points.reserve(numPoints);
    for (uint32_t i = 0; i < numPoints; ++i) area.m_points.push_back({points[i * 2], points[i * 2 + 1]});
    area.m_minX = integers[0];
    area.m_minY = integers[1];
    area.m_maxX = integers[6];
    area.m_maxY = integers[7];
    return area;
}

// ---- boat route ------------------------------------------------------------------------------------------------------------------------

// Flag 1 (Unload) / 2 (Destroy) remember the point's index. A point past the tenth is dropped.
inline void AddBoatPoint(map::BoatRoute& route, float x, float y, float z, int flags)
{
    if (route.m_numPoints < 0 || route.m_numPoints >= map::kMaxBoatPoints) return;
    route.m_points[static_cast<size_t>(route.m_numPoints)] = {x, y, z};
    if (flags == 1)
        route.m_unloadPointIndex = route.m_numPoints;
    else if (flags == 2)
        route.m_destroyPointIndex = route.m_numPoints;
    ++route.m_numPoints;
}

// Where the ferry appears (entry) or leaves (exit).
inline void SetBoatEnd(map::BoatEnd& end, float x, float y, float z, int number, float dx, float dy)
{
    end.m_pos[0] = x;
    end.m_pos[1] = y;
    end.m_pos[2] = z;
    end.m_number = number;
    end.m_dir[0] = dx;
    end.m_dir[1] = dy;
}

// ---- the area of a world point ---------------------------------------------------------------------------------------------------------

namespace detail {
struct Vec3 {
    float c[3];
};
// The world point of a polygon vertex: the tile origin of grid_math.
inline Vec3 VertexWorld(int tileSize, uint32_t vx, uint32_t vy)
{
    const grid_math::WorldPoint p = grid_math::TileToWorldPoint(tileSize, vx, vy, 0);
    return {{p.x, p.y, p.z}};
}
// Component differences computed in double and stored as floats, the argument of the length helper.
inline Vec3 RoundedDifference(const Vec3& a, const float p[3])
{
    Vec3 d;
    for (int i = 0; i < 3; ++i) d.c[i] = static_cast<float>(static_cast<double>(a.c[i]) - static_cast<double>(p[i]));
    return d;
}
// z * z + y * y + x * x in this order, in double.
inline double Length(const Vec3& d)
{
    const double z = d.c[2], y = d.c[1], x = d.c[0];
    return std::sqrt(z * z + y * y + x * x);
}
} // namespace detail

// The area of a world point, over the beach or the sea list: the index of the last area whose inclusive tile rectangle
// contains the tile of the point. Without one: the polygon vertex of the first area that is nearest to the point (measured
// with the mixed double/float rounding below) and then, over the later areas, every vertex that is nearer than the origin
// of the best tile so far; the answer is the index of the area of the last such vertex, 0 when none was.
// Returns false (no answer) for an empty list, a first polygon without points or a tile size below 2.
inline bool FindAreaOfPoint(const Grid& grid, const std::vector<Area>& areas, float x, float y, float z, int& result)
{
    if ((static_cast<uint32_t>(grid.m_tileSize) >> 1) == 0 || grid.m_width == 0 || grid.m_height == 0) return false;
    const grid_math::TileXY tile = grid_math::WorldPointToTile(grid.m_tileSize, grid.m_width, grid.m_height, x, y);
    const int tx = static_cast<int>(tile.x), ty = static_cast<int>(tile.y);
    int found = -1, index = 0;
    for (const Area& node : areas) {
        if (tx >= node.m_minX && tx <= node.m_maxX && ty >= node.m_minY && ty <= node.m_maxY) found = index;
        ++index;
    }
    if (found != -1) {
        result = found;
        return true;
    }
    if (areas.empty() || areas[0].m_points.empty()) return false;
    const Area& first = areas[0];

    const float point[3] = {x, y, z};
    uint32_t bestX = first.m_points[0].x, bestY = first.m_points[0].y;
    // The first area: a vertex replaces the best one when its distance (kept in the register, one rounding to float on the store) is smaller than
    // the distance of the best one measured from the float-rounded component differences (the register is compared against the stored float).
    for (size_t i = 1; i < first.m_points.size(); ++i) {
        const uint32_t vx = first.m_points[i].x, vy = first.m_points[i].y;
        const detail::Vec3 world = detail::VertexWorld(grid.m_tileSize, vx, vy);
        const double dx = static_cast<double>(world.c[0]) - x, dy = static_cast<double>(world.c[1]) - y,
                     dz = static_cast<double>(world.c[2]) - z;
        const float distVertex = static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
        const double distBest = detail::Length(detail::RoundedDifference(detail::VertexWorld(grid.m_tileSize, bestX, bestY), point));
        if (distBest > static_cast<double>(distVertex)) {
            bestX = vx;
            bestY = vy;
        }
    }
    int answer = 0;
    for (size_t nodeIndex = 1; nodeIndex < areas.size(); ++nodeIndex) {
        for (const map::TilePoint& vertex : areas[nodeIndex].m_points) {
            const uint32_t vx = vertex.x, vy = vertex.y;
            const float distVertex =
                    static_cast<float>(detail::Length(detail::RoundedDifference(detail::VertexWorld(grid.m_tileSize, vx, vy), point)));
            const grid_math::WorldPoint origin = grid_math::TileToWorldPoint(grid.m_tileSize, bestX, bestY, 0);
            const double distBest = detail::Length(detail::RoundedDifference({{origin.x, origin.y, origin.z}}, point));
            if (distBest > static_cast<double>(distVertex)) {
                bestX = vx;
                bestY = vy;
                answer = static_cast<int>(nodeIndex);
            }
        }
    }
    result = answer;
    return true;
}

} // namespace obl::grid_areas
