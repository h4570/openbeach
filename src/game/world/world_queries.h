#pragma once

#include "core/float_to_int.h"
#include "core/ascii_case.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"

#include "game/map/path_tiles.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

namespace obl::world_queries {

// A walk point must lie on pavement, next to a building or on a bit-2 cell, and not on a blocked or occupied cell.
constexpr uint32_t kWalkPointAccepted = path::kTilePavement | path::kTileNearBuilding | path::kTileBit2;
constexpr uint32_t kWalkPointRejected = path::kTileBlocked | path::kTileOccupied;

// Each axis stays at 53-bit precision until the candidate's single float store.
inline float RandomWalkCoordinate(int draw, double span)
{
    constexpr float kRandScale = 0x1.0002p-15f; // 1/32767 as a float, not a double
    return static_cast<float>((static_cast<double>(draw) * kRandScale) * span + 1.0);
}

// Env supplies the game's RNG and the read-only MapView::WorldToTile / grid query.
template<class Env> void FindRandomWalkPoint(float* out, Env& env)
{
    for (;;) {
        float point[3];
        point[0] = RandomWalkCoordinate(env.Rand(), 3998.0);
        point[1] = RandomWalkCoordinate(env.Rand(), 7998.0);
        point[2] = 0.0f;
        const uint32_t flags = env.TileFlags(point);
        if ((flags & kWalkPointAccepted) != 0 && (flags & kWalkPointRejected) == 0) {
            std::memcpy(out, point, sizeof(point));
            return;
        }
        // No retry cap and no fallback point: the draw repeats until a cell qualifies.
    }
}

// The fallback is the complete direction word; only its low byte controls traversal.
inline uint32_t AdjacentId(const std::vector<object::Base*>& sorted, uint32_t id, uint32_t forward)
{
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (sorted[i]->m_id != id) continue;
        const size_t adjacent = static_cast<uint8_t>(forward) ? (i + 1) % sorted.size() : (i + sorted.size() - 1) % sorted.size();
        return sorted[adjacent]->m_id;
    }
    return forward;
}

inline uint32_t GetAdjacentBuildingByName(const object::Manager& objects, uint32_t id, uint32_t forward)
{
    std::vector<object::Base*> sorted;
    sorted.reserve(objects.m_buildings.size());
    for (uint32_t i = 0; i < objects.m_buildings.size(); ++i) sorted.push_back(objects.m_buildings[i]);
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
        return static_cast<const object::Building*>(a)->m_definition->m_Name <
                static_cast<const object::Building*>(b)->m_definition->m_Name;
    });
    return AdjacentId(sorted, id, forward);
}

inline uint32_t GetAdjacentStaffByName(const object::Manager& objects, uint32_t id, int type, uint32_t forward)
{
    std::vector<object::Base*> sorted;
    for (uint32_t i = 0; i < objects.m_others.size(); ++i) {
        auto* object = objects.m_others[i];
        if (static_cast<int>(object->m_type) == type) sorted.push_back(object);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) { return ascii::LessUpper(a->m_Name, b->m_Name); });
    return AdjacentId(sorted, id, forward);
}

inline uint32_t GetCircuitPointCount(const world::World& world, int circuit)
{
    return world.m_numCircuitPoints[circuit];
}

inline world::CircuitPoint* GetCircuitPoints(world::World& world, int circuit)
{
    return world.m_circuitPoints[circuit];
}

inline void AddCircuitPoint(world::World& world, int circuit, int x, int y)
{
    // Append without a bounds check; scenario scripts provide the index and capacity.
    GetCircuitPoints(world, circuit)[world.m_numCircuitPoints[circuit]] = {x, y};
    ++world.m_numCircuitPoints[circuit];
}

inline int GetHour(const world::World& world)
{
    return FloatToIntWrap(std::floor(static_cast<double>(world.m_gameDayTime)));
}

inline int GetResortRating(const world::World& world)
{
    const auto& objects = *world.m_objectManager;
    if (!objects.m_guests.size()) return 0;
    double sum = 0.0;
    for (uint32_t i = 0; i < objects.m_guests.size(); ++i) sum += static_cast<const object::Guest*>(objects.m_guests[i])->m_happiness;
    // The 53-bit precision control uses 53-bit precision; there is no intermediate float store.
    const uint32_t rating = static_cast<uint32_t>(FloatToIntWrap(sum / objects.m_guests.size() * 100.0));
    return rating >= 96 ? 100 : static_cast<int>(rating);
}

inline bool IsBuildingInScenario(const world::World& world, int id)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(world.m_numValidBuildings); ++i)
        if (world.m_validBuildingIDs[i] == id) return true;
    return false;
}

// Value of the global string: BuildingDef::m_Tree of a building without a predecessor.
inline constexpr const char* kTreeNone = "none";

inline bool IsNamed(const object::Base& object, const char* name)
{
    return ascii::EqualsNoCase(object.m_Name, name);
}

inline bool AnyBuildingNamed(const object::Manager& objects, const char* name)
{
    for (uint32_t i = 0; i < objects.m_buildings.size(); ++i)
        if (IsNamed(*static_cast<object::Building*>(objects.m_buildings[i]), name)) return true;
    return false;
}

// Both build-tree queries resolve the definition (global WorldParams) only after the scenario check, and
// reject a single-instance type that already exists (MAXINSTANCES 1) before looking at its predecessor.
template<class Env> bool IsBuildingAllowed(const world::World& world, int id, Env& env)
{
    if (!IsBuildingInScenario(world, id)) return false;
    const tables::BuildingDef& def = *env.BuildingDef(id);
    const auto& objects = *world.m_objectManager;
    if (def.m_MaxInstances == 1 && AnyBuildingNamed(objects, def.m_key.c_str())) return false;
    if (ascii::EqualsNoCase(def.m_Tree, kTreeNone)) return true;
    return AnyBuildingNamed(objects, def.m_Tree.c_str()); // Any instance counts, even one under construction.
}

// "Locked" is true when the predecessor has no finished instance, and also for a NONE tree (not player-buildable).
// The scan goes on after the first finished predecessor, so every name match asks for its jobs.
template<class Env> bool IsBuildingLockedByTree(const world::World& world, int id, Env& env)
{
    if (!IsBuildingInScenario(world, id)) return false;
    const tables::BuildingDef& def = *env.BuildingDef(id);
    const auto& objects = *world.m_objectManager;
    if (def.m_MaxInstances == 1 && AnyBuildingNamed(objects, def.m_key.c_str())) return false;
    if (ascii::EqualsNoCase(def.m_Tree, kTreeNone)) return true;
    bool finished = false;
    for (uint32_t i = 0; i < objects.m_buildings.size(); ++i) {
        auto* building = static_cast<object::Building*>(objects.m_buildings[i]);
        if (IsNamed(*building, def.m_Tree.c_str()) && !env.HasBuildJob(building) && !env.HasDemolishJob(building)) finished = true;
    }
    return !finished;
}

template<class Env> int CountBuildingsOfType(const world::World& world, int id, bool finished, Env& env)
{
    // The definition is resolved through the global WorldParams even for an empty list.
    const char* key = env.BuildingKey(id);
    const auto& objects = *world.m_objectManager;
    int result = 0;
    for (uint32_t i = 0; i < objects.m_buildings.size(); ++i) {
        auto* building = static_cast<object::Building*>(objects.m_buildings[i]);
        if (!ascii::EqualsNoCase(building->m_Name, key)) continue;
        if (finished && (env.HasBuildJob(building) || env.HasDemolishJob(building))) continue;
        ++result;
    }
    return result;
}

} // namespace obl::world_queries
