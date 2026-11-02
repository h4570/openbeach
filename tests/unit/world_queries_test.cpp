#include "unit_test.h"
#include "game/world/world_queries.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/world/world_state.h"
#include "game/objects/object_building.h"

#include <array>
#include <bit>
#include <limits>
#include <memory>

using namespace obl;

OBL_TEST_CASE("world_queries.random_walk_coordinates_match_float_constants")
{
    struct Golden {
        int draw;
        uint32_t x, y;
    };
    constexpr Golden cases[] = {
            {0, 0x3f800000, 0x3f800000},
            {1, 0x3f8f9e1f, 0x3f9f3e3e},
            {2, 0x3f9f3c3e, 0x3fbe7c7d},
            {127, 0x4183f718, 0x41fffe10},
            {1024, 0x42fbe1f4, 0x437af1f4},
            {8191, 0x447a1a25, 0x44fa0a24},
            {16383, 0x44f9fe0c, 0x4579fe0c},
            {16384, 0x44fa01f4, 0x457a01f4},
            {32766, 0x4579ee0c, 0x45f9f60c},
            {32767, 0x4579f000, 0x45f9f800},
    };
    for (const auto& c : cases) {
        OBL_CHECK_EQ(std::bit_cast<uint32_t>(world_queries::RandomWalkCoordinate(c.draw, 3998.0)), c.x);
        OBL_CHECK_EQ(std::bit_cast<uint32_t>(world_queries::RandomWalkCoordinate(c.draw, 7998.0)), c.y);
    }
}

OBL_TEST_CASE("world_queries.random_walk_accepts_any_allowed_bit_and_preserves_output_guards")
{
    struct Env {
        uint32_t flags;
        int draws = 0, queries = 0;
        int Rand() { return draws++ == 0 ? 0 : 32767; }
        uint32_t TileFlags(float* p)
        {
            ++queries;
            OBL_CHECK_EQ(draws, 2);
            OBL_CHECK_EQ(p[0], 1.0f);
            OBL_CHECK_EQ(p[1], 7999.0f);
            OBL_CHECK_EQ(std::bit_cast<uint32_t>(p[2]), 0u);
            return flags;
        }
    };
    for (uint32_t flags : {2u, 0x80u, 0x10000000u, 0x10000082u, 0xffff7fefu}) {
        Env env{flags};
        float output[5] = {123, -1, -1, -1, 456};
        world_queries::FindRandomWalkPoint(output + 1, env);
        OBL_CHECK_EQ(output[0], 123.0f);
        OBL_CHECK_EQ(output[1], 1.0f);
        OBL_CHECK_EQ(output[2], 7999.0f);
        OBL_CHECK_EQ(output[3], 0.0f);
        OBL_CHECK_EQ(output[4], 456.0f);
        OBL_CHECK_EQ(env.queries, 1);
        OBL_CHECK_EQ(env.draws, 2);
    }
}

OBL_TEST_CASE("world_queries.random_walk_rejects_blocked_tiles_without_retry_limit")
{
    struct Env {
        float* out;
        int draws = 0, queries = 0;
        int Rand() { return draws++ % 2 == 0 ? 32767 : 0; }
        uint32_t TileFlags(float* p)
        {
            OBL_CHECK_EQ(draws, 2 * (queries + 1));
            OBL_CHECK_EQ(p[0], 3999.0f);
            OBL_CHECK_EQ(p[1], 1.0f);
            OBL_CHECK_EQ(out[0], -1.0f); // Rejected candidates never reach the output.
            OBL_CHECK_EQ(out[1], -2.0f);
            OBL_CHECK_EQ(out[2], -3.0f);
            constexpr uint32_t rejected[] = {0, 1, 0x10, 0x8000, 0x12, 0x8080, 0x10008000, 0x10000092};
            return queries++ < 1024 ? rejected[(queries - 1) % std::size(rejected)] : 0x80;
        }
    };
    float out[3] = {-1, -2, -3};
    Env env{out};
    world_queries::FindRandomWalkPoint(out, env);
    OBL_CHECK_EQ(env.queries, 1025);
    OBL_CHECK_EQ(env.draws, 2050);
    OBL_CHECK_EQ(out[0], 3999.0f);
    OBL_CHECK_EQ(out[1], 1.0f);
    OBL_CHECK_EQ(std::bit_cast<uint32_t>(out[2]), 0u);
}

OBL_TEST_CASE("world_queries.adjacent_buildings_use_definition_names_and_stable_wrap")
{
    auto objects = std::make_unique<object::Manager>();
    std::array<object::Building, 6> buildings{};
    auto defs = std::make_unique<std::array<tables::BuildingDef, 6>>();
    const char* names[] = {"Z", "A", "A", "a", "\xff", "\x80"};
    for (uint32_t i = 0; i < buildings.size(); ++i) {
        buildings[i].m_id = 90 - i * 10;
        buildings[i].m_definition = &(*defs)[i];
        buildings[i].m_waitingForDeath = 1;
        std::strcpy(buildings[i].m_Name, "IGNORED");
        (*defs)[i].m_Name = names[i];
    }
    // No building listed yet: the call answers with the direction itself.
    for (uint32_t forward : {0u, 1u, 0x100u, 0x101u})
        OBL_CHECK_EQ(world_queries::GetAdjacentBuildingByName(*objects, 90, forward), forward);
    for (auto& building : buildings) objects->AddBuilding(&building, building.m_id);
    const auto allBuildings = objects->m_buildings;
    objects->m_buildings.resize(1);
    OBL_CHECK_EQ(world_queries::GetAdjacentBuildingByName(*objects, 90, 0), 90u);
    OBL_CHECK_EQ(world_queries::GetAdjacentBuildingByName(*objects, 90, 1), 90u);
    objects->m_buildings = allBuildings;
    // IDs are deliberately in descending order; equal names retain input order.
    const uint32_t order[] = {80, 70, 90, 60, 40, 50};
    for (size_t i = 0; i < std::size(order); ++i) {
        OBL_CHECK_EQ(world_queries::GetAdjacentBuildingByName(*objects, order[i], 1), order[(i + 1) % 6]);
        OBL_CHECK_EQ(world_queries::GetAdjacentBuildingByName(*objects, order[i], 2), order[(i + 1) % 6]);
        OBL_CHECK_EQ(world_queries::GetAdjacentBuildingByName(*objects, order[i], 0x100), order[(i + 5) % 6]);
    }
    OBL_CHECK_EQ(world_queries::GetAdjacentBuildingByName(*objects, 123, 0x101), 0x101u);
    buildings[2].m_id = 80;
    OBL_CHECK_EQ(world_queries::GetAdjacentBuildingByName(*objects, 80, 1), 80u);
    OBL_CHECK_EQ(world_queries::GetAdjacentBuildingByName(*objects, 80, 0), 50u);
}

OBL_TEST_CASE("world_queries.adjacent_staff_filter_exact_class_and_stably_sort_names")
{
    auto objects = std::make_unique<object::Manager>();
    std::array<object::Base, 5> staff{};
    const char* names[] = {"Z", "A", "A", "a", "!"};
    for (uint32_t i = 0; i < staff.size(); ++i) {
        staff[i].m_id = i + 10;
        staff[i].m_type = i == 4 ? object::ObjectType::Mechanic : object::ObjectType::ServiceStaff;
        staff[i].m_waitingForDeath = 1;
        std::strcpy(staff[i].m_Name, names[i]);
        objects->AddOther(&staff[i], staff[i].m_id);
    }
    const auto allStaff = objects->m_others;
    objects->m_others.clear();
    OBL_CHECK_EQ(world_queries::GetAdjacentStaffByName(*objects, 10, 6, 1), 1u);
    objects->m_others = allStaff;
    const uint32_t order[] = {11, 12, 13, 10}; // case is ignored, and equal names keep their order
    for (size_t i = 0; i < std::size(order); ++i) {
        OBL_CHECK_EQ(world_queries::GetAdjacentStaffByName(*objects, order[i], 6, 1), order[(i + 1) % 4]);
        OBL_CHECK_EQ(world_queries::GetAdjacentStaffByName(*objects, order[i], 6, 0), order[(i + 3) % 4]);
    }
    OBL_CHECK_EQ(world_queries::GetAdjacentStaffByName(*objects, 14, 8, 1), 14u);
    OBL_CHECK_EQ(world_queries::GetAdjacentStaffByName(*objects, 14, 6, 0x101), 0x101u);
    for (int type : {-1, 262, 9}) OBL_CHECK_EQ(world_queries::GetAdjacentStaffByName(*objects, 10, type, 2), 2u);
}

OBL_TEST_CASE("world_queries.circuit_append_preserves_other_bytes_and_returns_live_storage")
{
    // The circuits live in the Boats group; the rest of the World owns memory and is not compared.
    layout::World world{};
    auto& boats = static_cast<obl::world::Boats&>(world);
    std::memset(static_cast<void*>(&boats), 0xa5, sizeof(boats));
    world.m_numCircuitPoints[0] = world.m_numCircuitPoints[1] = 0;
    obl::world::Boats expected = boats;
    for (int count = 0; count < 40; ++count) {
        for (int circuit = 0; circuit < 2; ++circuit) {
            OBL_CHECK_EQ(world_queries::GetCircuitPointCount(world, circuit), static_cast<uint32_t>(count));
            OBL_CHECK(world_queries::GetCircuitPoints(world, circuit) == &world.m_circuitPoints[circuit][0]);
            int x = count == 0 ? std::numeric_limits<int>::min() : count * 17 - 100;
            int y = count == 39 ? std::numeric_limits<int>::max() : 75 - count * 11;
            expected.m_circuitPoints[circuit][count] = {x, y};
            expected.m_numCircuitPoints[circuit] = count + 1;
            world_queries::AddCircuitPoint(world, circuit, x, y);
            OBL_CHECK_EQ(std::memcmp(&boats, &expected, sizeof(expected)), 0);
        }
    }
    OBL_CHECK_EQ(world_queries::GetCircuitPointCount(world, 0), 40u);
    OBL_CHECK_EQ(world_queries::GetCircuitPointCount(world, 1), 40u);
    world_queries::GetCircuitPoints(world, 1)[39].x = -123;
    OBL_CHECK_EQ(world.m_circuitPoints[1][39].x, -123);
}

OBL_TEST_CASE("world_queries.hour_floor_and_truncation")
{
    layout::World world{};
    for (auto [hour, expected] : std::array<std::pair<float, int>, 7>{{{0, 0}, {23.999f, 23}, {-0.01f, -1}, {24, 24}, {4294967296.0f, 0},
                 {std::numeric_limits<float>::quiet_NaN(), 0}, {std::numeric_limits<float>::infinity(), 0}}}) {
        world.m_gameDayTime = hour;
        OBL_CHECK_EQ(world_queries::GetHour(world), expected);
    }
}

OBL_TEST_CASE("world_queries.rating_empty_mean_truncation_and_unsigned_cap")
{
    auto objects = std::make_unique<object::Manager>();
    layout::World world{};
    world.m_objectManager.reset(objects.get());
    OBL_CHECK_EQ(world_queries::GetResortRating(world), 0);
    object::Guest a{}, b{};
    objects->AddGuest(&a, 0);
    objects->AddGuest(&b, 1);
    a.m_happiness = 0.5f;
    b.m_happiness = 1;
    OBL_CHECK_EQ(world_queries::GetResortRating(world), 75);
    a.m_happiness = b.m_happiness = 0.96f;
    OBL_CHECK_EQ(world_queries::GetResortRating(world), 95); // 0.96f is just below 0.96.
    a.m_happiness = b.m_happiness = 0.96001f;
    OBL_CHECK_EQ(world_queries::GetResortRating(world), 100);
    a.m_happiness = b.m_happiness = -0.5f;
    OBL_CHECK_EQ(world_queries::GetResortRating(world), 100);
    a.m_happiness = b.m_happiness = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK_EQ(world_queries::GetResortRating(world), 0);
}

OBL_TEST_CASE("world_queries.scenario_list_uses_count_and_accepts_arbitrary_ids")
{
    layout::World world{};
    world.m_validBuildingIDs[0] = -7;
    OBL_CHECK(!world_queries::IsBuildingInScenario(world, -7));
    world.m_numValidBuildings = 128;
    world.m_validBuildingIDs[127] = 300;
    OBL_CHECK(world_queries::IsBuildingInScenario(world, -7));
    OBL_CHECK(world_queries::IsBuildingInScenario(world, 300));
    OBL_CHECK(!world_queries::IsBuildingInScenario(world, 301));
}

OBL_TEST_CASE("world_queries.count_by_exact_name_and_short_circuit_jobs")
{
    auto objects = std::make_unique<object::Manager>();
    layout::World world{};
    world.m_objectManager.reset(objects.get());
    struct Env {
        int definitions = 0, buildCalls = 0, demolishCalls = 0;
        const char* BuildingKey(int id)
        {
            OBL_CHECK_EQ(id, 17);
            ++definitions;
            return "BAR";
        }
        bool HasBuildJob(object::Building* b)
        {
            ++buildCalls;
            return b->m_id == 1;
        }
        bool HasDemolishJob(object::Building* b)
        {
            ++demolishCalls;
            return b->m_id == 2;
        }
    } env;
    OBL_CHECK_EQ(world_queries::CountBuildingsOfType(world, 17, false, env), 0);
    OBL_CHECK_EQ(env.definitions, 1);
    std::array<object::Building, 5> buildings{};
    for (uint32_t i = 0; i < buildings.size(); ++i) {
        buildings[i].m_id = i;
        std::strcpy(buildings[i].m_Name, i == 4 ? "bar" : "BAR");
        objects->AddBuilding(&buildings[i], buildings[i].m_id);
    }
    OBL_CHECK_EQ(world_queries::CountBuildingsOfType(world, 17, false, env), 5);
    OBL_CHECK_EQ(env.buildCalls, 0);
    OBL_CHECK_EQ(world_queries::CountBuildingsOfType(world, 17, true, env), 3);
    OBL_CHECK_EQ(env.buildCalls, 5);
    OBL_CHECK_EQ(env.demolishCalls, 4);
}

namespace {
struct TreeFixture {
    std::unique_ptr<object::Manager> objects = std::make_unique<object::Manager>();
    std::unique_ptr<tables::BuildingDef> def = std::make_unique<tables::BuildingDef>();
    layout::World world{};
    std::array<object::Building, 4> buildings{};
    int definitions = 0, buildCalls = 0, demolishCalls = 0;

    TreeFixture()
    {
        world.m_objectManager.reset(objects.get());
        world.m_numValidBuildings = 1;
        world.m_validBuildingIDs[0] = 9;
        def->m_key = "BAR";
        def->m_Tree = "KIOSK";
        for (uint32_t i = 0; i < buildings.size(); ++i) {
            buildings[i].m_id = i;
            std::strcpy(buildings[i].m_Name, "#");
            objects->AddBuilding(&buildings[i], buildings[i].m_id);
        }
    }
    tables::BuildingDef* BuildingDef(int id)
    {
        OBL_CHECK_EQ(id, 9);
        ++definitions;
        return def.get();
    }
    bool HasBuildJob(object::Building* b)
    {
        ++buildCalls;
        return b->m_id == 1;
    }
    bool HasDemolishJob(object::Building* b)
    {
        ++demolishCalls;
        return b->m_id == 2;
    }
};
} // namespace

OBL_TEST_CASE("world_queries.build_tree_scenario_gate_skips_definition_lookup")
{
    TreeFixture f;
    f.world.m_validBuildingIDs[0] = 8;
    OBL_CHECK(!world_queries::IsBuildingAllowed(f.world, 9, f));
    OBL_CHECK(!world_queries::IsBuildingLockedByTree(f.world, 9, f));
    OBL_CHECK_EQ(f.definitions, 0);
}

OBL_TEST_CASE("world_queries.allowed_single_instance_and_predecessor")
{
    TreeFixture f;
    OBL_CHECK(!world_queries::IsBuildingAllowed(f.world, 9, f)); // Predecessor KIOSK absent.
    std::strcpy(f.buildings[3].m_Name, "KIOSK");
    OBL_CHECK(world_queries::IsBuildingAllowed(f.world, 9, f)); // Job state of the predecessor is irrelevant here.
    OBL_CHECK_EQ(f.buildCalls + f.demolishCalls, 0);
    f.def->m_MaxInstances = 1;
    OBL_CHECK(world_queries::IsBuildingAllowed(f.world, 9, f));
    std::strcpy(f.buildings[0].m_Name, "BAR");
    OBL_CHECK(!world_queries::IsBuildingAllowed(f.world, 9, f)); // The single instance exists.
    f.def->m_MaxInstances = 2;
    OBL_CHECK(world_queries::IsBuildingAllowed(f.world, 9, f));
    std::strcpy(f.buildings[3].m_Name, "kiosk");
    OBL_CHECK(world_queries::IsBuildingAllowed(f.world, 9, f)); // Names ignore case.
    f.def->m_Tree = "NONE";
    OBL_CHECK(world_queries::IsBuildingLockedByTree(f.world, 9, f));
    f.def->m_Tree = "KIOSK";
    f.def->m_Tree = "none";
    OBL_CHECK(world_queries::IsBuildingAllowed(f.world, 9, f));
    f.objects->m_buildings.clear();
    OBL_CHECK(world_queries::IsBuildingAllowed(f.world, 9, f));
}

OBL_TEST_CASE("world_queries.locked_requires_finished_predecessor_and_scans_all")
{
    TreeFixture f;
    OBL_CHECK(world_queries::IsBuildingLockedByTree(f.world, 9, f)); // No predecessor at all.
    std::strcpy(f.buildings[1].m_Name, "KIOSK"); // Under construction.
    std::strcpy(f.buildings[2].m_Name, "KIOSK"); // Being demolished.
    OBL_CHECK(world_queries::IsBuildingLockedByTree(f.world, 9, f));
    OBL_CHECK_EQ(f.buildCalls, 2);
    OBL_CHECK_EQ(f.demolishCalls, 1); // Only the instance without a build job asks about demolition.
    f.buildCalls = f.demolishCalls = 0;
    std::strcpy(f.buildings[0].m_Name, "KIOSK"); // Finished.
    std::strcpy(f.buildings[3].m_Name, "KIOSK");
    OBL_CHECK(!world_queries::IsBuildingLockedByTree(f.world, 9, f));
    OBL_CHECK_EQ(f.buildCalls, 4); // No short circuit after the first finished one.
    OBL_CHECK_EQ(f.demolishCalls, 3);
}

OBL_TEST_CASE("world_queries.locked_none_tree_and_existing_single_instance")
{
    TreeFixture f;
    f.def->m_Tree = "NONE";
    OBL_CHECK(world_queries::IsBuildingLockedByTree(f.world, 9, f)); // Not player-buildable.
    f.def->m_MaxInstances = 1;
    std::strcpy(f.buildings[2].m_Name, "BAR");
    OBL_CHECK(!world_queries::IsBuildingLockedByTree(f.world, 9, f)); // The single-instance exit comes first.
    OBL_CHECK_EQ(f.buildCalls + f.demolishCalls, 0);
}
