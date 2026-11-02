#pragma once

#include "core/float_to_int.h"
#include "game/map/grid_math.h"
#include "game/map/grid_state.h"
#include "game/objects/guest_beach_points.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/objects/object_guest.h"
#include "game/ui/sprite.h"
#include "game/objects/object_staff.h"
#include "script_message.h"

#include <cmath>
#include <cstdint>
#include <vector>

// The seven helpers of the lifeguard script (docs/architecture/objects.md; the lifeguard's CallFunction dispatcher calls them
// for the ids 0x2f, 0x89, 0x8a, 0x8b, 0x8f, 0x90, 0x91). All take only the lifeguard and return nothing.
//
// Every function answers `false` on input it cannot handle (an empty or missing area chain, a missing home building, the sea chain running
// out); the caller then runs its fallback code, which has not been touched at that point (no draw consumed,
// nothing written, nothing sent). The arithmetic is written in `double` (53-bit precision).
//
// Env (everything outside the lifeguard and the grid):
//   int Rand()                                              the game's rand()
//   void Post(const messages::Message&)                       posts to the message handler; sender = receiver = the lifeguard, the fields not listed stay 0
//   object::Base* Object(int32_t id)                  the object manager's lookup
//   void ServePosition(const object::Base*, float out[3])   GetServePosition (building_api.h)
//   void WorldToTile(const float world[3], int32_t tile[2])       the map view world-to-tile conversion
//   void Teleport(float x, float y, float z)                the lifeguard's virtual Teleport
namespace obl::staff::lifeguard {

constexpr int32_t kMsgMove2Point = 5; // MT_MOVE2POINT: flags = x, data = y (truncated world coordinates)
constexpr int32_t kMsgPavement2Point = 8; // MT_PAVEMENT2POINT
constexpr int32_t kMsgPavement2Object = 9; // MT_PAVEMENT2OBJECT: data = the building
constexpr int32_t kMsgSwim2SeaPoint = 11; // MT_SWIM2SEAPOINT
constexpr int32_t kMsgPathThroughScenery = 13; // MT_PATH_THROUGH_SCENERY
constexpr uint32_t kPathBlockedLimit = 10; // FindBeachPoint: from this many blocked paths on, the counter resets and the message changes
constexpr uint32_t kPathBlockedStuck = 0x14; // ... and at exactly this many it is MT_PATH_THROUGH_SCENERY

namespace detail {

inline messages::Message Make(const object::Lifeguard& employee, int32_t type, int32_t flags, int32_t data)
{
    messages::Message message{};
    message.sender = static_cast<int32_t>(employee.m_id);
    message.receiver = static_cast<int32_t>(employee.m_id);
    message.type = type;
    message.flags = flags;
    message.data = data;
    return message;
}

// The direction of a building's sprite (the sprite's direction word): 2 and 3 are the sides the tower's serve point is shifted to.
inline int32_t SpriteDirection(const object::Base& building)
{
    return static_cast<const ui::SpriteInstance*>(building.m_sprite)->m_direction;
}

// The serve point of the home building moved the way both SetDrawPos* functions do: x by -29 / +29 for the direction 2 / 3, y by +30 for either;
// the shifted x is kept at 53 bits until the single store to float, y is stored as float after its add.
inline void ShiftedServePoint(const object::Base& home, float point[3])
{
    const int32_t direction = SpriteDirection(home);
    if (direction == 2)
        point[0] = static_cast<float>(static_cast<double>(point[0]) - 29.0);
    else if (direction == 3)
        point[0] = static_cast<float>(static_cast<double>(point[0]) + 29.0);
    else
        return;
    point[1] = static_cast<float>(static_cast<double>(point[1]) + 30.0);
}

} // namespace detail

// ---- finding the lifeguard beach () --------------------------------------------------------------------------------------------------------
// Picks the beach node nearest to the lifeguard: every vertex of every node is measured (distance in the registers, sqrt(dz^2 + dy^2 + dx^2) of the
// unrounded differences) against the best distance so far, which is kept as a float. A vertex wins when it is nearer (a NaN counts as
// nearer), and the first vertex always wins, as does any vertex after a best of exactly 0 (the "no best yet" test is `best == 0`). Writes the area
// (`m_beachArea`) and its position in the chain (`m_BeachArea`). No beach area: answers false.
inline bool FindBeach(const map::Grid& grid, object::Lifeguard& employee)
{
    if (grid.m_beachAreas.empty()) return false;
    const double ox = employee.m_WorldXYZ[0], oy = employee.m_WorldXYZ[1], oz = employee.m_WorldXYZ[2];
    const map::Area* bestArea = &grid.m_beachAreas[0];
    int32_t bestIndex = 0, index = 0;
    float best = 0.0f;
    for (const map::Area& area : grid.m_beachAreas) {
        for (const map::TilePoint& vertex : area.m_points) {
            const grid_math::WorldPoint at = grid_math::TileToWorldPoint(grid.m_tileSize, vertex.x, vertex.y, 0);
            const double dx = static_cast<double>(at.x) - ox, dy = static_cast<double>(at.y) - oy, dz = static_cast<double>(at.z) - oz;
            const double distance = std::sqrt(dz * dz + dy * dy + dx * dx);
            const bool nearer = !(distance >= static_cast<double>(best)); // less or unordered
            const bool noBest = !(best < 0.0f) && !(best > 0.0f); // equal to zero or unordered
            if (nearer || noBest) {
                bestIndex = index;
                best = static_cast<float>(distance);
                bestArea = &area;
            }
        }
        ++index;
    }
    employee.m_beachArea = bestArea;
    employee.m_BeachArea = bestIndex;
    return true;
}

// ---- finding a beach point () ---------------------------------------------------------------------------------------------------
// A random sand tile of the lifeguard's beach node (`m_beachArea`): per axis `trunc(rand * 3.0518509e-5 * float(max - min) + float(min))`, x first,
// redrawn until the tile is land (flag 2) without a building (0x10); no bound on the retries. Then the lifeguard sends itself a walk to the tile
// origin (truncated x in flags, y in data): MT_PAVEMENT2POINT while fewer than 10 paths were blocked; otherwise the counter resets and it is
// MT_PATH_THROUGH_SCENERY when it was exactly 20 and MT_MOVE2POINT for any other value.
template<class Env> bool FindBeachPoint(const map::Grid& grid, object::Lifeguard& employee, Env& env)
{
    const map::Area* area = employee.m_beachArea;
    if (!area) return false;
    const float spanX = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(area->m_maxX) - static_cast<uint32_t>(area->m_minX)));
    const float baseX = static_cast<float>(area->m_minX);
    const float spanY = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(area->m_maxY) - static_cast<uint32_t>(area->m_minY)));
    const float baseY = static_cast<float>(area->m_minY);
    const double unit = static_cast<double>(script::kRandUnit);
    auto draw = [&](float span, float base) {
        const double scaled = static_cast<double>(env.Rand()) * unit * static_cast<double>(span);
        return static_cast<uint32_t>(FloatToIntWrap(scaled + static_cast<double>(base)));
    };
    uint32_t x = draw(spanX, baseX), y = draw(spanY, baseY);
    for (;;) {
        const uint32_t flags = grid.Cell(x, y).m_flags;
        if ((flags & 2) != 0 && (flags & 0x10) == 0) break;
        x = draw(spanX, baseX);
        y = draw(spanY, baseY);
    }
    const grid_math::WorldPoint at = grid_math::TileToWorldPoint(grid.m_tileSize, x, y, 0);
    const int32_t px = FloatToIntWrap(static_cast<double>(at.x)), py = FloatToIntWrap(static_cast<double>(at.y));
    const uint32_t blocked = static_cast<uint32_t>(employee.m_timesPathBlocked);
    int32_t type = kMsgPavement2Point;
    if (blocked >= kPathBlockedLimit) {
        employee.m_timesPathBlocked = 0;
        type = blocked == kPathBlockedStuck ? kMsgPathThroughScenery : kMsgMove2Point;
    }
    env.Post(detail::Make(employee, type, px, py));
    return true;
}

// ---- moving to the tower () -------------------------------------------------------------------------------------------------------
// Sends the lifeguard to its tower (the home building, `m_HomeID`) according to the building's sprite direction: 0 and 1 walk the pavement to the
// building (MT_PAVEMENT2OBJECT, flags 0, data = the building); 2 and 3 walk to the serve point shifted by -29 / +29 in x and +30 in y
// (MT_MOVE2POINT, x kept at 53 bits and truncated, y stored as float first); any other direction sends nothing.
template<class Env> bool MoveToTower(object::Lifeguard& employee, Env& env)
{
    const object::Base* home = env.Object(employee.m_HomeID);
    if (!home) return false;
    const int32_t direction = detail::SpriteDirection(*home);
    if (direction == 0 || direction == 1) {
        env.Post(detail::Make(employee, kMsgPavement2Object, 0, static_cast<int32_t>(home->m_id)));
        return true;
    }
    if (direction != 2 && direction != 3) return true;
    float serve[3];
    env.ServePosition(home, serve);
    const double x = direction == 2 ? static_cast<double>(serve[0]) - 29.0 : static_cast<double>(serve[0]) + 29.0;
    const float y = static_cast<float>(static_cast<double>(serve[1]) + 30.0);
    env.Post(detail::Make(employee, kMsgMove2Point, FloatToIntWrap(x), FloatToIntWrap(static_cast<double>(y))));
    return true;
}

// ---- placing the lifeguard at the building () / SetDrawPosServe () ----------------------------------------------------------------------
// Depth bookkeeping around the lifeguard's tower: the tile under the home building's (shifted) serve point gets its `m_zOffset` raised by 500
// (Build) or lowered by 500 (Serve), 16-bit wrapping, and the lifeguard is teleported: Build to the shifted serve point (z of the serve point),
// Serve to where it already stands.
template<class Env> bool SetDrawPos(const map::Grid& grid, object::Lifeguard& employee, Env& env, bool build)
{
    const object::Base* home = env.Object(employee.m_HomeID);
    if (!home) return false;
    const float own[3] = {employee.m_WorldXYZ[0], employee.m_WorldXYZ[1], employee.m_WorldXYZ[2]};
    float point[3];
    env.ServePosition(home, point);
    detail::ShiftedServePoint(*home, point);
    int32_t tile[2];
    env.WorldToTile(point, tile);
    map::GridCell& cell = grid.Cell(static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1]));
    cell.m_zOffset = static_cast<uint16_t>(cell.m_zOffset + (build ? 500 : -500));
    if (build)
        env.Teleport(point[0], point[1], point[2]);
    else
        env.Teleport(own[0], own[1], own[2]);
    return true;
}

// ---- moving to the sea entry () / heading for shore () ---------------------------------------------------------------
// Both look for the shore vertex of the sea chain (`Area Sea`, shut or not) nearest to the lifeguard whose tile and diagonal neighbours 0, 2, 4 and 6 carry
// no building (flag 0x10): the first free vertex of the chain, improved by every strictly nearer free vertex of the rest of its node and of the later
// nodes (the same search as FindNearestSeaPoint, but measured from the lifeguard in 3-D). A node that runs out hands over to the next one;
// reaching the end of the chain answers false, as does a missing chain or a node without points.
// The vertex's point (tile-to-world conversion kind 4) is the goal: MT_MOVE2POINT (Move2SeaEntry) or MT_SWIM2SEAPOINT (HeadForShore) to itself with the
// truncated x and y. Move2SeaEntry also stores the point in `m_ExitPoint` and marks the rescued guest (`m_saving_id`) with action state 1
// (a field of that object).
struct ShoreVertex {
    uint32_t x, y;
};

inline bool NearestFreeShoreVertex(const map::Grid& grid, const float (&from)[3], ShoreVertex& out)
{
    using guest_beach::detail::CloserThan;
    using guest_beach::detail::Origin;
    using guest_beach::detail::ShoreFree;
    const std::vector<map::Area>& areas = grid.m_seaAreas;
    if (areas.empty() || areas[0].m_points.empty()) return false;
    size_t cur = 0, i = 0;
    while (!ShoreFree(grid, areas[cur].m_points[i].x, areas[cur].m_points[i].y)) {
        if (++i == areas[cur].m_points.size()) {
            ++cur;
            if (cur == areas.size() || areas[cur].m_points.empty()) return false;
            i = 0;
        }
    }
    ShoreVertex best{areas[cur].m_points[i].x, areas[cur].m_points[i].y};
    auto consider = [&](const map::Area& node, size_t first) {
        for (size_t j = first; j < node.m_points.size(); ++j) {
            const uint32_t vx = node.m_points[j].x, vy = node.m_points[j].y;
            if (CloserThan(Origin(grid, vx, vy), Origin(grid, best.x, best.y), from[0], from[1], from[2]) && ShoreFree(grid, vx, vy))
                best = {vx, vy};
        }
    };
    consider(areas[cur], i + 1);
    for (size_t node = cur + 1; node < areas.size(); ++node) consider(areas[node], 0);
    out = best;
    return true;
}

template<class Env> bool GoToShore(const map::Grid& grid, object::Lifeguard& employee, Env& env, bool seaEntry)
{
    object::Base* guest = nullptr;
    if (seaEntry) {
        guest = env.Object(employee.m_saving_id);
        if (!guest) return false;
    }
    const float from[3] = {employee.m_WorldXYZ[0], employee.m_WorldXYZ[1], employee.m_WorldXYZ[2]};
    ShoreVertex vertex;
    if (!NearestFreeShoreVertex(grid, from, vertex)) return false;
    if (guest) static_cast<object::Guest*>(guest)->m_ActionState = 1;
    const grid_math::WorldPoint point = grid_math::TileToWorldPoint(grid.m_tileSize, vertex.x, vertex.y, 4);
    if (seaEntry) {
        employee.m_ExitPoint[0] = point.x;
        employee.m_ExitPoint[1] = point.y;
        employee.m_ExitPoint[2] = point.z;
    }
    env.Post(detail::Make(employee, seaEntry ? kMsgMove2Point : kMsgSwim2SeaPoint, FloatToIntWrap(static_cast<double>(point.x)),
            FloatToIntWrap(static_cast<double>(point.y))));
    return true;
}

} // namespace obl::staff::lifeguard
