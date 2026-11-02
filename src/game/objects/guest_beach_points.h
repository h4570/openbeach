#pragma once

#include "game/map/grid_math.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_tables.h"
#include "script_message.h"

#include <cmath>
#include <cstdint>
#include <vector>

// The pure rules of three guest helpers of SearchDesire (guest beach search, docs/architecture/objects.md): SetRepeatAction, FindSunbathePoint and FindSwimPoint. Everything runs on the game's own grid
// (map::Grid) and its beach / sea area chains; the draws come from a callable that is the game's rand().
//
// Arithmetic runs at 53-bit precision: a candidate vertex is measured with its coordinate differences stored as float, the best
// vertex so far with the unrounded double differences, both summed as (dx^2 + dz^2) + dy^2 before the square root, and a candidate wins only when
// strictly closer (NaN never wins). `rand * * width` is truncated with script::FloatToIntWrap.
namespace obl::guest_beach {

// SetRepeatAction: RepeatAction = trunc(MAX_<desire> * value) for the desires the switch lists, then 0 becomes 1. MAX_<desire> is loaded as
// an unsigned 64-bit integer (the value zero-extended), the product is one 53-bit multiply, and the compare against 1 is unsigned (only 0
// is replaced). Sunbathing (12), swimming (13), stroll (16), headache (4), sleep and scared have no branch and keep the previous value.
constexpr bool RepeatUsesMax(int desire)
{
    return (desire >= 0 && desire <= 3) || (desire >= 5 && desire <= 11) || desire == 14 || desire == 15 || (desire >= 17 && desire <= 20);
}

inline int32_t RepeatAction(int desire, const std::vector<int>& max, float value, int32_t previous)
{
    int32_t repeat = previous;
    if (RepeatUsesMax(desire))
        repeat = script::FloatToIntWrap(static_cast<double>(static_cast<uint32_t>(max[desire])) * static_cast<double>(value));
    return repeat == 0 ? 1 : repeat;
}

// Result buffers of the finders (shared by all callers): the point of the chosen vertex and the point the guest walks to.
struct BeachPoints {
    float vertex[3];
    float target[3];
};

// What a finder did. The third result hands the decision back to the caller: an empty first area and a sea chain that runs out
// are not modelled; no draw has been consumed when it is returned.
enum class Found { Done, Nothing, Unhandled };

namespace detail {

using grid_math::WorldPoint;

inline WorldPoint Origin(const map::Grid& grid, uint32_t x, uint32_t y)
{
    return grid_math::TileToWorldPoint(grid.m_tileSize, x, y, 0);
}

// `cand` is stored as float differences, `best` stays double; z differences are measured against `refZ` (0 for the swim finder, which ignores z).
// The sums are (dx^2 + dz^2) + dy^2 in the sunbathe and swim finders and (dz^2 + dy^2) + dx^2 in WanderBeach (both candidate and best); the two
// orders only differ in the last bit when z is not 0, which the swim-like finders never have (tile origins have z = 0 and refZ is 0).
enum class SumOrder { XZY, ZYX };

template<SumOrder kOrder = SumOrder::XZY>
inline bool CloserThan(const WorldPoint& cand, const WorldPoint& best, float refX, float refY, float refZ)
{
    auto sum = [](double x, double y, double z) { return kOrder == SumOrder::XZY ? (x * x + z * z) + y * y : (z * z + y * y) + x * x; };
    const float dx = static_cast<float>(static_cast<double>(cand.x) - refX);
    const float dy = static_cast<float>(static_cast<double>(cand.y) - refY);
    const float dz = static_cast<float>(static_cast<double>(cand.z) - refZ);
    const double c = std::sqrt(sum(dx, dy, dz));
    const double b =
            std::sqrt(sum(static_cast<double>(best.x) - refX, static_cast<double>(best.y) - refY, static_cast<double>(best.z) - refZ));
    return c < b;
}

struct Vertex {
    uint32_t x, y;
    const map::Area* area;
};

inline uint32_t Flags(const map::Grid& grid, uint32_t x, uint32_t y)
{
    return grid.Cell(x, y).m_flags;
}

inline grid_math::TileXY Step(const map::Grid& grid, uint32_t x, uint32_t y, int direction)
{
    return grid_math::Neighbour(grid.m_width, grid.m_height, x, y, direction);
}

// One random tile of an area's inclusive rectangle: truncated `rand * scale * float(max - min)` per axis, x first. Wraps like the 32-bit adds.
struct RectDraw {
    int32_t minX, minY;
    float spanX, spanY;
    explicit RectDraw(const map::Area& area)
        : minX(area.m_minX),
          minY(area.m_minY),
          spanX(static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(area.m_maxX) - static_cast<uint32_t>(area.m_minX)))),
          spanY(static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(area.m_maxY) - static_cast<uint32_t>(area.m_minY))))
    {}

    template<class Rand> grid_math::TileXY Draw(Rand& rand) const
    {
        const double fx = static_cast<double>(rand()) * static_cast<double>(script::kRandUnit);
        const uint32_t x = static_cast<uint32_t>(script::FloatToIntWrap(fx * spanX)) + static_cast<uint32_t>(minX);
        const double fy = static_cast<double>(rand()) * static_cast<double>(script::kRandUnit);
        const uint32_t y = static_cast<uint32_t>(script::FloatToIntWrap(fy * spanY)) + static_cast<uint32_t>(minY);
        return {x, y};
    }
};

// Sand tile with free surroundings: the tile and all eight neighbours are land (2), carry no building (0x10) and none of 0x8100.
inline bool SandFree(const map::Grid& grid, uint32_t x, uint32_t y)
{
    auto good = [](uint32_t f) { return (f & 2) != 0 && (f & 0x10) == 0 && (f & 0x8100) == 0; };
    if (!good(Flags(grid, x, y))) return false;
    for (int direction = 0; direction < 8; ++direction) {
        const grid_math::TileXY n = Step(grid, x, y, direction);
        if (!good(Flags(grid, n.x, n.y))) return false;
    }
    return true;
}

// Shore vertex accepted by the swim finder: its own tile and the diagonal neighbours 0, 2, 4, 6 carry no building (the low byte is tested).
inline bool ShoreFree(const map::Grid& grid, uint32_t x, uint32_t y)
{
    if ((Flags(grid, x, y) & 0x10) != 0) return false;
    for (int direction : {0, 2, 4, 6}) {
        const grid_math::TileXY n = Step(grid, x, y, direction);
        if ((Flags(grid, n.x, n.y) & 0x10) != 0) return false;
    }
    return true;
}

// Open water for swimming: water (4) without a building, and the tiles two steps away in 0/2/4/6 are water or land (0xc). Direction 6 is
// stepped twice only for the first draw; a redraw recomputes it with one step (`sixSteps` = 1).
inline bool OpenWater(const map::Grid& grid, uint32_t x, uint32_t y, int sixSteps)
{
    const uint32_t f = Flags(grid, x, y);
    if ((f & 4) == 0 || (f & 0x10) != 0) return false;
    for (int direction : {0, 2, 4, 6}) {
        grid_math::TileXY n = Step(grid, x, y, direction);
        if (direction != 6 || sixSteps == 2) n = Step(grid, n.x, n.y, direction);
        if ((Flags(grid, n.x, n.y) & 0xc) == 0) return false;
    }
    return true;
}

} // namespace detail

// FindSunbathePoint: the beach vertex nearest to the guest (every vertex of every node of the chain) picks the node; a random tile of its
// rectangle is redrawn until it is free sand, at most 1000 redraws (after the 1000th the vertex itself, even when that draw was good).
template<class Rand> Found FindSunbathePoint(const map::Grid& grid, const float (&guest)[3], Rand&& rand, BeachPoints& out)
{
    using detail::Vertex;
    const std::vector<map::Area>& areas = grid.m_beachAreas;
    if (areas.empty() || areas[0].m_points.empty()) return Found::Unhandled;

    Vertex best{areas[0].m_points[0].x, areas[0].m_points[0].y, &areas[0]};
    for (const map::Area& node : areas)
        for (size_t i = &node == &areas[0] ? 1 : 0; i < node.m_points.size(); ++i) {
            const uint32_t x = node.m_points[i].x, y = node.m_points[i].y;
            if (detail::CloserThan(detail::Origin(grid, x, y), detail::Origin(grid, best.x, best.y), guest[0], guest[1], guest[2]))
                best = {x, y, &node};
        }

    const detail::RectDraw rect(*best.area);
    grid_math::TileXY tile = rect.Draw(rand);
    int redraws = 0;
    while (!detail::SandFree(grid, tile.x, tile.y) && redraws < 1000) {
        tile = rect.Draw(rand);
        ++redraws;
    }
    if (redraws == 1000) tile = {best.x, best.y};

    const grid_math::WorldPoint vertex = grid_math::TileToWorldPoint(grid.m_tileSize, best.x, best.y, 4);
    const grid_math::WorldPoint target = grid_math::TileToWorldPoint(grid.m_tileSize, tile.x, tile.y, 4);
    out = {{vertex.x, vertex.y, vertex.z}, {target.x, target.y, target.z}};
    return Found::Done;
}

namespace detail {

// The sea search shared by FindSwimPoint and FindNearestSeaPoint: from the first usable sea node, the first
// free shore vertex (a node that runs out hands over to the next one without any filter), improved by every strictly nearer free vertex of that
// node and of the later usable nodes. The target is a random open-water tile of the winning node's rectangle. FindSwimPoint skips shut nodes
// (`honourShut`) and answers `Nothing` when all are shut; the nearest-sea search takes every node, so only an empty chain hands the decision back.
template<class Rand> Found SeaSearch(const map::Grid& grid, float x, float y, Rand&& rand, BeachPoints& out, bool honourShut)
{
    using detail::Vertex;
    const std::vector<map::Area>& areas = grid.m_seaAreas;
    size_t cur = 0;
    while (honourShut && cur < areas.size() && areas[cur].m_shut) ++cur;
    if (cur == areas.size()) return honourShut ? Found::Nothing : Found::Unhandled;
    if (areas[cur].m_points.empty()) return Found::Unhandled;

    size_t i = 0;
    while (!detail::ShoreFree(grid, areas[cur].m_points[i].x, areas[cur].m_points[i].y)) {
        if (++i == areas[cur].m_points.size()) {
            ++cur;
            if (cur == areas.size() || areas[cur].m_points.empty())
                return Found::Unhandled; // FatalError "I hope this never happens" / unvalidated empty node
            i = 0;
        }
    }
    Vertex best{areas[cur].m_points[i].x, areas[cur].m_points[i].y, &areas[cur]};
    auto consider = [&](const map::Area& node, size_t from) {
        for (size_t j = from; j < node.m_points.size(); ++j) {
            const uint32_t vx = node.m_points[j].x, vy = node.m_points[j].y;
            if (detail::CloserThan(detail::Origin(grid, vx, vy), detail::Origin(grid, best.x, best.y), x, y, 0.0f) &&
                    detail::ShoreFree(grid, vx, vy))
                best = {vx, vy, &node};
        }
    };
    consider(areas[cur], i + 1);
    for (size_t node = cur + 1; node < areas.size(); ++node)
        if (!honourShut || !areas[node].m_shut) consider(areas[node], 0);

    const detail::RectDraw rect(*best.area);
    grid_math::TileXY tile = rect.Draw(rand);
    for (int sixSteps = 2; !detail::OpenWater(grid, tile.x, tile.y, sixSteps); sixSteps = 1) tile = rect.Draw(rand);

    const grid_math::WorldPoint vertex = grid_math::TileToWorldPoint(grid.m_tileSize, best.x, best.y, 4);
    const grid_math::WorldPoint target = grid_math::TileToWorldPoint(grid.m_tileSize, tile.x, tile.y, 0);
    out = {{vertex.x, vertex.y, vertex.z}, {target.x, target.y, target.z}};
    return Found::Done;
}

} // namespace detail

// FindSwimPoint: the sea search over the open nodes. `Nothing` = no flag-0 node (nothing is written).
template<class Rand> Found FindSwimPoint(const map::Grid& grid, float x, float y, Rand&& rand, BeachPoints& out)
{
    return detail::SeaSearch(grid, x, y, rand, out, true);
}

// FindNearestSeaPoint: the same search over every node, shut or not (no node at all hands the decision back).
template<class Rand> Found FindNearestSeaPoint(const map::Grid& grid, float x, float y, Rand&& rand, BeachPoints& out)
{
    return detail::SeaSearch(grid, x, y, rand, out, false);
}

// ---- WanderBeach --------------------------------------------------------------------------------------------------------------

// A tile WanderBeach accepts: land (2) without a building (0x10), inside the 128 x 128 map (signed compares: a wrapped negative draw is rejected).
inline bool WanderTileOk(const map::Grid& grid, uint32_t x, uint32_t y)
{
    const uint32_t flags = detail::Flags(grid, x, y);
    return (flags & 2) != 0 && (flags & 0x10) == 0 && static_cast<int32_t>(x) >= 0 && static_cast<int32_t>(x) <= 0x7f &&
            static_cast<int32_t>(y) >= 0 && static_cast<int32_t>(y) <= 0x7f;
}

// No beach or an empty first beach node: the caller ends in fatal::UnhandledInput.
inline bool WanderDeclined(const map::Grid& grid)
{
    return grid.m_beachAreas.empty() || grid.m_beachAreas[0].m_points.empty();
}

// The beach vertex nearest to the guest (3-D distance, sums in (dz^2 + dy^2) + dx^2 order; the first node is scanned from its second vertex, every
// later node from its first) picks a node; a random tile of that node's rectangle is redrawn until WanderTileOk (no cap), and the middle of that tile
// is the walking goal. Needs !WanderDeclined.
template<class Rand> grid_math::WorldPoint WanderTarget(const map::Grid& grid, const float (&guest)[3], Rand&& rand)
{
    using detail::Vertex;
    const std::vector<map::Area>& areas = grid.m_beachAreas;
    Vertex best{areas[0].m_points[0].x, areas[0].m_points[0].y, &areas[0]};
    auto consider = [&](const map::Area& node, size_t from) {
        for (size_t i = from; i < node.m_points.size(); ++i) {
            const uint32_t x = node.m_points[i].x, y = node.m_points[i].y;
            if (detail::CloserThan<detail::SumOrder::ZYX>(
                        detail::Origin(grid, x, y), detail::Origin(grid, best.x, best.y), guest[0], guest[1], guest[2]))
                best = {x, y, &node};
        }
    };
    consider(areas[0], 1);
    for (size_t node = 1; node < areas.size(); ++node) consider(areas[node], 0);

    const detail::RectDraw rect(*best.area);
    grid_math::TileXY tile = rect.Draw(rand);
    while (!WanderTileOk(grid, tile.x, tile.y)) tile = rect.Draw(rand);
    const grid_math::PathPoint center = grid_math::TileToWorldCenter(grid.m_tileSize, tile.x, tile.y);
    return {center.x, center.y, 0.0f};
}

} // namespace obl::guest_beach
