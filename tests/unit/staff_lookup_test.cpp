#include "object_storage.h"
#include "game/objects/object_guest.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_leaves.h"
#include "unit_test.h"

#include "game/objects/staff_lookup.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

using namespace obl;
using namespace obl::world_data;
using obl::staff::StateKind;

namespace {

constexpr uint32_t kPavement = 0x10000000;
constexpr uint32_t kBlocked = 0x8000;

// The grid as a function of the world point; every question is logged.
struct GridEnv {
    std::function<uint32_t(float, float, float)> flags = [](float, float, float) { return 0u; };
    std::vector<int> draws{0};
    size_t drawn = 0;
    std::vector<std::vector<float>> asked;
    int Rand()
    {
        const int value = draws[std::min(drawn, draws.size() - 1)];
        ++drawn;
        return value;
    }
    uint32_t CellFlags(float x, float y, float z)
    {
        asked.push_back({x, y, z});
        return flags(x, y, z);
    }
    uint32_t TileFlags(float x, float y, float z) { return CellFlags(x, y, z); }
};

std::unique_ptr<object::Staff> Staff()
{
    auto staff = std::make_unique<object::Staff>();
    obl::unit::FillObject<object::Staff>(*staff);
    return staff;
}

} // namespace

OBL_TEST_CASE("staff_lookup.walkable_point_tries_the_four_diagonals_in_order")
{
    GridEnv env;
    float pos[3] = {100.0f, 200.0f, 7.0f};
    OBL_CHECK(!staff::FindWalkablePointNear(pos, 32, 16, env));
    OBL_REQUIRE(env.asked.size() == 4);
    OBL_CHECK(env.asked[0] == (std::vector<float>{132.0f, 184.0f, 7.0f})); // +dx, -dy
    OBL_CHECK(env.asked[1] == (std::vector<float>{68.0f, 216.0f, 7.0f})); // -dx, +dy
    OBL_CHECK(env.asked[2] == (std::vector<float>{68.0f, 184.0f, 7.0f})); // -dx, -dy
    OBL_CHECK(env.asked[3] == (std::vector<float>{132.0f, 216.0f, 7.0f})); // +dx, +dy
    OBL_CHECK(pos[0] == 100.0f && pos[1] == 200.0f && pos[2] == 7.0f); // a miss leaves the point alone
}

OBL_TEST_CASE("staff_lookup.walkable_point_takes_the_first_pavement_tile_and_keeps_z")
{
    GridEnv env;
    env.flags = [](float x, float, float) { return x < 100.0f ? kPavement : 0u; }; // only the -dx tiles are pavement
    float pos[3] = {100.0f, 200.0f, 7.0f};
    OBL_CHECK(staff::FindWalkablePointNear(pos, 32, 16, env));
    OBL_CHECK_EQ(env.asked.size(), size_t{2}); // the second diagonal answers
    OBL_CHECK(pos[0] == 68.0f && pos[1] == 216.0f && pos[2] == 7.0f);
}

OBL_TEST_CASE("staff_lookup.walkable_point_is_the_pavement_predicate")
{
    for (uint32_t flags : {0x10000000u, 0x80u, 0x2u, 0x10000000u | 0x80u, 0x2u | 0x4u}) {
        GridEnv env;
        env.flags = [flags](float, float, float) { return flags; };
        float pos[3] = {0, 0, 0};
        OBL_CHECK(staff::FindWalkablePointNear(pos, 1, 1, env));
    }
    for (uint32_t flags : {0u, 0x10000000u | kBlocked, 0x80u | kBlocked, 0x2u | 0x10u, 0x2u | kBlocked, 0x80000000u, 0x4u, 0x100u}) {
        GridEnv env;
        env.flags = [flags](float, float, float) { return flags; };
        float pos[3] = {0, 0, 0};
        OBL_CHECK(!staff::FindWalkablePointNear(pos, 1, 1, env));
    }
    // The blocked bit refuses both alternatives, whatever else the tile has.
    GridEnv env;
    env.flags = [](float, float, float) { return 0x2u | 0x10000000u | kBlocked; };
    float pos[3] = {0, 0, 0};
    OBL_CHECK(!staff::FindWalkablePointNear(pos, 1, 1, env));
}

OBL_TEST_CASE("staff_lookup.walkable_point_rounds_the_sum_once_to_float")
{
    GridEnv env;
    env.flags = [](float, float, float) { return kPavement; };
    float pos[3] = {16777216.0f, 3.0f, 0.0f}; // 2^24: +1 is not representable, the sum rounds back (to even)
    OBL_CHECK(staff::FindWalkablePointNear(pos, 1, 0, env));
    OBL_CHECK(pos[0] == 16777216.0f);
    float far[3] = {0.1f, 0.2f, 0.0f};
    OBL_CHECK(staff::FindWalkablePointNear(far, 3, 5, env));
    OBL_CHECK(far[0] == static_cast<float>(static_cast<double>(0.1f) + 3.0));
    OBL_CHECK(far[1] == static_cast<float>(static_cast<double>(0.2f) - 5.0));
}

OBL_TEST_CASE("staff_lookup.wander_offset_formula_and_draw_order")
{
    auto staff = Staff();
    staff->m_WorldXYZ[0] = 1000.0f;
    staff->m_WorldXYZ[1] = 2000.0f;
    staff->m_WorldXYZ[2] = 5.0f;
    GridEnv env;
    env.draws = {0, 32767};
    env.flags = [](float, float, float) { return 0x2u; };
    staff::FindWanderPos(*staff, env);
    OBL_CHECK_EQ(env.drawn, size_t{2});
    OBL_CHECK(staff->m_WanderPos[0] == 700.0f); // draw 0: -300
    OBL_CHECK(staff->m_WanderPos[1] == 2299.0f); // draw 32767: the scale is a float just below 1/32767, so the span truncates to 599
    OBL_CHECK(staff->m_WanderPos[2] == 0.0f); // z of the wander point is not touched
    // Truncation toward zero of the negative product: the middle of the range lands on -1 or 0, not on a rounded value.
    GridEnv middle;
    middle.draws = {16383, 16384};
    middle.flags = [](float, float, float) { return 0x2u; };
    staff::FindWanderPos(*staff, middle);
    OBL_CHECK(staff->m_WanderPos[0] == 999.0f);
    OBL_CHECK(staff->m_WanderPos[1] == 2000.0f);
}

OBL_TEST_CASE("staff_lookup.wander_resets_a_coordinate_that_leaves_the_map")
{
    auto staff = Staff();
    staff->m_WorldXYZ[0] = 10.0f; // -300 puts x below zero: it falls back to the member's own x
    staff->m_WorldXYZ[1] = 8000.0f;
    GridEnv env;
    env.draws = {0, 32767}; // y + 299 stays inside 8192
    env.flags = [](float, float, float) { return 0x2u; };
    staff::FindWanderPos(*staff, env);
    OBL_CHECK(staff->m_WanderPos[0] == 10.0f);
    OBL_CHECK(staff->m_WanderPos[1] == 8000.0f); // 8000 + 299 = 8299 >= 8192: back to the member's own y
    staff->m_WorldXYZ[0] = 4000.0f;
    staff->m_WorldXYZ[1] = 100.0f;
    GridEnv edge;
    edge.draws = {32767, 0}; // x + 299 = 4299 >= 4096 resets, y - 300 < 0 resets
    edge.flags = [](float, float, float) { return 0x2u; };
    staff::FindWanderPos(*staff, edge);
    OBL_CHECK(staff->m_WanderPos[0] == 4000.0f);
    OBL_CHECK(staff->m_WanderPos[1] == 100.0f);
}

OBL_TEST_CASE("staff_lookup.wander_retries_until_a_standable_tile_or_the_try_limit")
{
    auto staff = Staff();
    staff->m_WorldXYZ[0] = 1000.0f;
    staff->m_WorldXYZ[1] = 1000.0f;
    // The third sample (draws 4 and 5) is the first good tile.
    GridEnv env;
    env.draws = {0, 0, 0, 0, 32767, 32767};
    int asks = 0;
    env.flags = [&asks](float, float, float) { return ++asks < 3 ? 0x1u : 0x10000000u; };
    staff::FindWanderPos(*staff, env);
    OBL_CHECK_EQ(env.drawn, size_t{6});
    OBL_CHECK(staff->m_WanderPos[0] == 1299.0f && staff->m_WanderPos[1] == 1299.0f);
    // A tile with the occupied bit is refused even when it is pavement; the search ends at the limit and keeps the last sample.
    GridEnv never;
    never.draws = {0, 32767};
    never.flags = [](float, float, float) { return 0x10000000u | 0x10u; };
    staff::FindWanderPos(*staff, never);
    OBL_CHECK_EQ(never.drawn, size_t{2002}); // 1001 samples of two draws
    OBL_CHECK_EQ(never.asked.size(), size_t{1001});
}

OBL_TEST_CASE("staff_lookup.has_state_ranges_match_the_state_switches")
{
    auto count = [](StateKind kind) {
        int n = 0;
        for (uint32_t state = 0; state < 0x200; ++state) n += staff::HasState(kind, state);
        return n;
    };
    OBL_CHECK_EQ(count(StateKind::Cleaner), 20);
    OBL_CHECK_EQ(count(StateKind::Lifeguard), 15);
    OBL_CHECK_EQ(count(StateKind::Mechanic), 13);
    OBL_CHECK_EQ(count(StateKind::Rep), 11);
    OBL_CHECK_EQ(count(StateKind::Security), 19);
    OBL_CHECK_EQ(count(StateKind::Builder), 15);
    OBL_CHECK(staff::HasState(StateKind::Cleaner, 0x68) && staff::HasState(StateKind::Cleaner, 0x75) &&
            !staff::HasState(StateKind::Cleaner, 0x76));
    OBL_CHECK(!staff::HasState(StateKind::Lifeguard, 0) && staff::HasState(StateKind::Lifeguard, 1) &&
            staff::HasState(StateKind::Lifeguard, 0x57));
    OBL_CHECK(staff::HasState(StateKind::Mechanic, 0x58) && staff::HasState(StateKind::Mechanic, 0x5e) &&
            !staff::HasState(StateKind::Mechanic, 0x5f));
    OBL_CHECK(staff::HasState(StateKind::Rep, 0xb) && !staff::HasState(StateKind::Rep, 0xc) && !staff::HasState(StateKind::Rep, 5));
    OBL_CHECK(staff::HasState(StateKind::Security, 0x76) && staff::HasState(StateKind::Security, 0x83) &&
            !staff::HasState(StateKind::Security, 0x84));
    OBL_CHECK(staff::HasState(StateKind::Builder, 0x4c) && !staff::HasState(StateKind::Builder, 0x4d) &&
            staff::HasState(StateKind::Builder, 0x84));
    OBL_CHECK(!staff::HasState(StateKind::Builder, 0xffffffffu) && !staff::HasState(StateKind::Cleaner, 0x80000000u));
}

OBL_TEST_CASE("staff_lookup.home_and_selection")
{
    auto staff = Staff();
    staff::SetHome(*staff, 57);
    OBL_CHECK_EQ(staff->m_HomeID, 57);
    staff::SetHome(*staff, 0xffffffffu);
    OBL_CHECK_EQ(staff->m_HomeID, -1);
    for (int32_t level : {0, 1, 3, -2}) {
        staff->m_SelectLevel = level;
        staff::ReduceSelectionToHover(*staff);
        OBL_CHECK_EQ(staff->m_SelectLevel, level);
    }
    staff->m_SelectLevel = 2;
    staff::ReduceSelectionToHover(*staff);
    OBL_CHECK_EQ(staff->m_SelectLevel, 1);
}
