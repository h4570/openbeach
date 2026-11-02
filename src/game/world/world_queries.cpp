#include "game/world/world_queries_api.h"

#include "game/game/game_accessors.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/game/rng.h"
#include "game/objects/building_api.h"
#include "game/objects/object_scratch.h"
#include "game/construction/construction_state.h"
#include "game/world/world_params.h"
#include "game/map/grid_state.h"
#include "core/call_counter.h"
#include "game/world/world_queries.h"

#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <type_traits>
#include "game/ui/map_view_math.h"

namespace obl::world {
namespace {
using layout::World;
using object::Building;
// Call counters (`call-report world.hour`, ...).
test::CallCounter g_queries[] = {
        test::CallCounter("world.hour"),
        test::CallCounter("world.resort-rating"),
        test::CallCounter("world.building-in-scenario"),
        test::CallCounter("world.count-buildings"),
        test::CallCounter("world.count-finished-buildings"),
        test::CallCounter("world.building-allowed"),
        test::CallCounter("world.building-locked-by-tree"),
        test::CallCounter("world.adjacent-building"),
        test::CallCounter("world.adjacent-staff"),
        test::CallCounter("world.circuit-point-count"),
        test::CallCounter("world.circuit-points"),
        test::CallCounter("world.add-circuit-point"),
        test::CallCounter("world.random-walk-point"),
};

struct Env {
    int Rand() const { return game::Rand(); }
    uint32_t TileFlags(float* point) const
    {
        uint32_t tile[2];
        ui::MapView::WorldToTile(tile, point);
        return game::GetGrid()->Cell(tile[0], tile[1]).m_flags;
    }
    tables::BuildingDef* BuildingDef(int id) const { return world_params::GetBuildingDef(game::GetWorldParams(), id); }
    const char* BuildingKey(int id) const { return BuildingDef(id)->m_key.c_str(); }
    bool HasBuildJob(Building* building) const { return object::building_api::HasBuildJob(building); }
    bool HasDemolishJob(Building* building) const { return object::building_api::HasDemolishJob(building); }
};

template<class Compute> auto Answer(test::CallCounter& counter, Compute compute)
{
    counter.Count();
    return compute();
}

} // namespace

// The world queries. Each counts its own `world.*` counter.
} // namespace obl::world
namespace obl::world {

int World::Hour()
{
    return Answer(g_queries[0], [&] { return world_queries::GetHour(*this); });
}
int World::ResortRating()
{
    return Answer(g_queries[1], [&] { return world_queries::GetResortRating(*this); });
}
bool World::BuildingInScenario(int id)
{
    return Answer(g_queries[2], [&] { return world_queries::IsBuildingInScenario(*this, id); });
}
int World::CountBuildings(int id)
{
    Env env;
    return Answer(g_queries[3], [&] { return world_queries::CountBuildingsOfType(*this, id, false, env); });
}
int World::CountFinishedBuildings(int id)
{
    Env env;
    return Answer(g_queries[4], [&] { return world_queries::CountBuildingsOfType(*this, id, true, env); });
}
bool World::BuildingAllowed(int id)
{
    Env env;
    return Answer(g_queries[5], [&] { return world_queries::IsBuildingAllowed(*this, id, env); });
}
bool World::BuildingLockedByTree(int id)
{
    Env env;
    return Answer(g_queries[6], [&] { return world_queries::IsBuildingLockedByTree(*this, id, env); });
}
uint32_t World::AdjacentBuilding(uint32_t id, uint32_t forward)
{
    return Answer(g_queries[7], [&] { return world_queries::GetAdjacentBuildingByName(*game::GetObjectManager(), id, forward); });
}
uint32_t World::AdjacentStaff(uint32_t id, int type, uint32_t forward)
{
    return Answer(g_queries[8], [&] { return world_queries::GetAdjacentStaffByName(*game::GetObjectManager(), id, type, forward); });
}
uint32_t World::CircuitPointCount(int circuit)
{
    return Answer(g_queries[9], [&] { return world_queries::GetCircuitPointCount(*this, circuit); });
}
CircuitPoint* World::CircuitPoints(int circuit)
{
    return Answer(g_queries[10], [&] { return world_queries::GetCircuitPoints(*this, circuit); });
}
void World::AddCircuitPoint(int circuit, int x, int y)
{
    g_queries[11].Count();
    world_queries::AddCircuitPoint(*this, circuit, x, y);
}
// Returns `out`: the guest desire search reads the point through the returned pointer.
float* World::RandomWalkPoint(float* out)
{
    Env env;
    g_queries[12].Count();
    world_queries::FindRandomWalkPoint(out, env);
    return out;
}
} // namespace obl::world
namespace obl::world {

namespace {

void DriveWalkPoint(int rounds)
{
    if (!game::GetGrid()) return;
    // Real map first, then private grids exercise each accepted bit and both exclusion bits.
    float point[3];
    for (int i = 0; i < rounds; ++i) World::RandomWalkPoint(point);
    struct RestoreGrid {
        map::Grid* saved = game::GetGrid();
        ~RestoreGrid() { game::SetGrid(saved); }
    } restore;
    map::Grid grid{};
    grid.m_cells.resize(77);
    auto& cells = grid.m_cells;
    game::SetGrid(&grid);
    grid.m_width = grid.m_height = 1;
    grid.m_tileSize = 64;
    for (uint32_t flags : {path::kTileBit2, path::kTileNearBuilding, path::kTilePavement, ~(path::kTileBlocked | path::kTileOccupied)}) {
        cells[0].m_flags = flags;
        for (int i = 0; i < rounds; ++i) World::RandomWalkPoint(point);
    }
    grid.m_width = 7;
    grid.m_height = 11;
    constexpr uint32_t kFlags[] = {0u, 1u, path::kTileOccupied, path::kTileBlocked, path::kTileOccupied | path::kTileBit2,
            path::kTileBlocked | path::kTileNearBuilding, path::kTilePavement | path::kTileBlocked | path::kTileOccupied, path::kTileBit2,
            path::kTileNearBuilding, path::kTilePavement};
    for (size_t i = 0; i < cells.size(); ++i) cells[i].m_flags = kFlags[i % std::size(kFlags)];
    for (int size : {32, 64, 128}) {
        grid.m_tileSize = size;
        for (int i = 0; i < rounds; ++i) World::RandomWalkPoint(point);
    }
}

// The fixtures vary the object counts per round: the first `n` of `backing` become the list, with their own ids.
template<class Backing> void FillList(std::vector<object::Base*>& list, std::vector<uint32_t>& ids, Backing& backing, size_t n)
{
    list.clear();
    ids.clear();
    for (size_t i = 0; i < n; ++i) {
        list.push_back(&backing[i]);
        ids.push_back(backing[i].m_id);
    }
}

void DriveAdjacent(World& world, int rounds)
{
    auto objects = std::make_unique<object::Manager>();
    struct RestoreObjects {
        object::Manager* saved = game::GetObjectManager();
        ~RestoreObjects() { game::SetObjectManager(saved); }
    } restore;
    game::SetObjectManager(objects.get());
    // World.m_objectManager intentionally points elsewhere: both functions read the global (now GetObjectManager()).
    // Bare fixtures: their teardown would give sprites back and leave grid cells (object::ScratchArray).
    object::ScratchArray<Building, 32> buildingFixtures;
    auto& buildings = *buildingFixtures;
    auto defs = std::make_unique<std::array<tables::BuildingDef, 32>>();
    object::ScratchArray<object::Base, 32> staffFixtures;
    auto& staff = *staffFixtures;
    constexpr const char* kNames[] = {"Z", "A", "A", "a", "", "\x80", "\xff", "AB"};
    for (uint32_t i = 0; i < buildings.size(); ++i) {
        buildings[i].m_id = 900 - i * 11;
        buildings[i].m_definition = &(*defs)[i];
        std::strcpy(buildings[i].m_Name, "same-key");
        staff[i].m_id = 600 - i * 7;
        staff[i].m_type = static_cast<object::ObjectType>(6 + i % 3);
        staff[i].m_waitingForDeath = buildings[i].m_waitingForDeath = i % 2;
    }
    for (int round = 0; round < rounds; ++round) {
        FillList(objects->m_buildings, objects->m_buildingIds, buildings, round % 33);
        FillList(objects->m_others, objects->m_otherIds, staff, round % 33);
        for (size_t i = 0; i < buildings.size(); ++i) {
            (*defs)[i].m_Name = kNames[(round * 3 + i * 5) % std::size(kNames)];
            std::strcpy(staff[i].m_Name, kNames[(round * 7 + i * 3) % std::size(kNames)]);
        }
        for (uint32_t forward : {0u, 1u, 2u, 0x100u, 0x101u}) {
            world.AdjacentBuilding(0xffffffff, forward);
            for (uint32_t i = 0; i < objects->m_buildings.size(); ++i) world.AdjacentBuilding(buildings[i].m_id, forward);
            for (int type : {6, 7, 8, 9, -1, 262}) {
                world.AdjacentStaff(0xffffffff, type, forward);
                for (uint32_t i = 0; i < objects->m_others.size(); ++i) world.AdjacentStaff(staff[i].m_id, type, forward);
            }
        }
    }
}

void DriveCircuits(int rounds)
{
    for (int round = 0; round < rounds; ++round) {
        World world{};
        auto& boats = static_cast<obl::world::Boats&>(world); // the circuits; the rest of the World owns memory
        std::memset(static_cast<void*>(&boats), 0xa5, sizeof(boats));
        for (int circuit = 0; circuit < 2; ++circuit) world.m_numCircuitPoints[circuit] = 0;
        for (int count = 0; count <= 40; ++count) {
            for (int circuit = 0; circuit < 2; ++circuit) {
                world.CircuitPointCount(circuit);
                world.CircuitPoints(circuit);
                if (count < 40) {
                    const int x = count == 0 ? std::numeric_limits<int>::min() : round * 131 - count * 7;
                    const int y = count == 39 ? std::numeric_limits<int>::max() : count * 19 - round * 71;
                    // Call the entry point through the outer script recorder, as real script commands do.
                    world.AddCircuitPoint(circuit, x, y);
                }
            }
        }
    }
}

// Build-tree rounds on the private world: the real definitions are edited in place for the round (single instance flag,
// predecessor NONE / another type / itself) and restored right after, so the loaded game never sees the change.
void DriveBuildTree(World& world, object::Manager& objects, std::array<Building, 4>& buildings, int count)
{
    auto* params = game::GetWorldParams();
    const int numDefs = params->m_numBuildings;
    for (int round = 0; round < count; ++round) {
        const int id = round % numDefs;
        auto& def = params->m_buildings[id];
        struct Restore {
            tables::BuildingDef& def;
            int maxInstances = def.m_MaxInstances;
            std::string tree;
            Restore(tables::BuildingDef& d) : def(d), tree(d.m_Tree) {}
            ~Restore()
            {
                def.m_MaxInstances = maxInstances;
                def.m_Tree = tree;
            }
        } restore(def);
        def.m_MaxInstances = (round & 1) ? 1 : restore.maxInstances;
        switch (round / numDefs % 3) {
        case 1: def.m_Tree = "none"; break;
        case 2: def.m_Tree = params->m_buildings[(id + 1) % numDefs].m_key; break;
        default: break;
        }
        // Per slot: 0 = unrelated name, 1 = the type itself, 2 = its predecessor. Slot 1 has a build job, slot 2 a demolish job.
        int pattern = (round * 37 + 11) % 81;
        for (auto& building : buildings) {
            const int kind = pattern % 3;
            pattern /= 3;
            std::strcpy(building.m_Name, kind == 0 ? "#" : kind == 1 ? def.m_key.c_str() : def.m_Tree.c_str());
        }
        FillList(objects.m_buildings, objects.m_buildingIds, buildings, round % 5);
        // Every fifth round leaves the type outside the scenario (the cheap early exit).
        world.m_numValidBuildings = round % 5 == 4 ? 0 : 1;
        world.m_validBuildingIDs[0] = id;
        world.BuildingAllowed(id);
        world.BuildingLockedByTree(id);
    }
}

void Drive(int count)
{
    auto* params = game::GetWorldParams();
    if (!game::GetWorld() || !params || params->m_numBuildings == 0) return;
    World world{};
    auto objects = std::make_unique<object::Manager>();
    world.m_objectManager.reset(objects.get());
    construction::Manager construction;
    world.m_constructionManager.reset(&construction);
    // The fixture lends both: the World must not free them.
    struct Lent {
        World& world;
        ~Lent()
        {
            (void)world.m_objectManager.release();
            (void)world.m_constructionManager.release();
        }
    } lent{world};
    // Has*Job reads the game context's world. A scoped substitution keeps its queries on the private fixture.
    struct RestoreWorld {
        World* saved = game::GetWorld();
        ~RestoreWorld() { game::GetContext().world = saved; }
    } restore;
    game::GetContext().world = &world;
    // Bare fixtures: their teardown would give sprites back and leave grid cells (object::ScratchArray).
    object::ScratchArray<object::Guest, 8> guestFixtures;
    object::ScratchArray<Building, 4> buildingFixtures;
    auto& guests = *guestFixtures;
    auto& buildings = *buildingFixtures;
    for (uint32_t i = 0; i < buildings.size(); ++i) buildings[i].m_id = 100 + i;
    auto& build = *construction.jobs.emplace_back(std::make_unique<construction::Job>());
    auto& demolish = *construction.jobs.emplace_back(std::make_unique<construction::Job>());
    build.m_buildingId = 101;
    demolish.m_buildingId = 102;
    build.m_underConstruction = 1;
    demolish.m_underDemolition = 1;
    constexpr float kHours[] = {0, 0.999f, 1, 23.999f, 24, -0.001f, -1, 2147483648.0f, 4294967296.0f,
            std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
    constexpr float kHappiness[] = {0, 0.01f, 0.95f, 0.96f, 0.959999f, 1, -0.5f, 2, std::numeric_limits<float>::quiet_NaN()};
    for (float hour : kHours) {
        world.m_gameDayTime = hour;
        world.Hour();
    }
    world.m_gameDayTime = 8;
    for (float happiness : kHappiness)
        for (uint32_t n = 0; n <= guests.size(); ++n) {
            FillList(objects->m_guests, objects->m_guestIds, guests, n);
            for (auto& guest : guests) guest.m_happiness = happiness;
            world.ResortRating();
        }
    for (int n : {0, 1, 128}) {
        world.m_numValidBuildings = n;
        for (int i = 0; i < n; ++i) world.m_validBuildingIDs[i] = i * 3;
        for (int id = -1; id <= 385; ++id) world.BuildingInScenario(id);
    }
    for (int round = 0; round < count; ++round) {
        int id = round % params->m_numBuildings;
        const char* key = params->m_buildings[id].m_key.c_str();
        for (auto& building : buildings) std::strcpy(building.m_Name, key);
        // Case-sensitive name mismatch, duplicate matches, and all three job states.
        buildings[3].m_Name[0] = '#';
        FillList(objects->m_buildings, objects->m_buildingIds, buildings, round % 5);
        world.CountBuildings(id);
        world.CountFinishedBuildings(id);
        FillList(objects->m_guests, objects->m_guestIds, guests, round % 9);
        for (size_t i = 0; i < guests.size(); ++i) guests[i].m_happiness = static_cast<float>((round * 17 + i * 13) % 101) / 100;
        world.ResortRating();
    }
    DriveBuildTree(world, *objects, buildings, count);
    DriveAdjacent(world, count);
    DriveCircuits(count);
    world.m_numValidBuildings = 0;
    game::GetContext().world = restore.saved;
    // Also cover every definition against the loaded world's real building/job lists.
    for (int id = 0; id < params->m_numBuildings; ++id) {
        restore.saved->CountBuildings(id);
        restore.saved->CountFinishedBuildings(id);
        restore.saved->BuildingInScenario(id);
        restore.saved->BuildingAllowed(id);
        restore.saved->BuildingLockedByTree(id);
    }
}
} // namespace

bool ConfigureWorldQueryDrivers()
{
    g_queries[0].SetDriver(&Drive); // The hour driver exercises every query in the shared fixture.
    g_queries[12].SetDriver(&DriveWalkPoint);
    return true;
}
} // namespace obl::world
