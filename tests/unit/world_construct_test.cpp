#include "dirty_world.h"
#include "game/world/world_state.h"
#include "unit_test.h"

#include "game/world/world_construct.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace obl;
using world_data::World;

namespace {

// Dirty memory, as operator_new hands it over: a step must not depend on the allocation being zeroed.
std::unique_ptr<World> DirtyWorld()
{
    auto world = std::make_unique<World>();
    DirtyGroups(*world);
    return world;
}

struct FakeEnv {
    int count = 0;
    std::vector<int> category;
    std::vector<int> objects;
    int BuildingCount() const { return count; }
    int Category(int id) const { return category[id]; }
    int ObjectsOf(int id) const { return objects[id]; }
};

} // namespace

OBL_TEST_CASE("world_construct.init_fields_clears_the_runtime_fields_and_keeps_the_records")
{
    auto world = DirtyWorld();
    world->m_profitRecord[3] = 12.5f;
    auto* cursor = reinterpret_cast<ui::Cursor*>(0x1000);
    auto* input = reinterpret_cast<ui::Input*>(0x2000);
    ui::ViewState view;
    DirtyView(view);
    world_construct::InitFields(*world, view, cursor, input, true, 1.0f);
    OBL_CHECK_EQ(world->m_loadingSave, 1);
    OBL_CHECK_EQ(world->m_startup, 1);
    OBL_CHECK_EQ(world->m_gameSpeed, 1.0f);
    OBL_CHECK_EQ(world->m_gameSpeedLevel, 0);
    OBL_CHECK_EQ(view.m_cursor, cursor);
    OBL_CHECK_EQ(view.m_input, input);
    OBL_CHECK_EQ(world->m_scriptVars[0], 0);
    OBL_CHECK_EQ(world->m_scriptVars[0x1f], 0);
    OBL_CHECK_EQ(world->m_numCircuitPoints[1], 0u);
    OBL_CHECK_EQ(view.m_hotkeysDisabled, 0);
    OBL_CHECK_EQ(view.m_darkenByDepth, 0);
    OBL_CHECK_EQ(view.m_devToggle, 0);
    OBL_CHECK_EQ(view.m_cameraDragging, 0);
    OBL_CHECK_EQ(view.m_drawObjectsArg, 0);
    OBL_CHECK_EQ(view.m_zoomRequest, 0); // reset with the whole view state; FinishFields sets it
    OBL_CHECK_EQ(world->m_numBuildings, 0);
    // Not written by the constructor: the clock-independent leftovers and the records.
    OBL_CHECK_EQ(world->m_profitRecord[3], 12.5f);
    OBL_CHECK_EQ(world->m_numValidBuildings, static_cast<int>(0xA5A5A5A5));
    OBL_CHECK_EQ(world->m_breakDownsAllowed, 0xA5);

    world_construct::InitFields(*world, view, cursor, input, false, 2.0f);
    OBL_CHECK_EQ(world->m_loadingSave, 0);
}

OBL_TEST_CASE("world_construct.day_clock_starts_at_nine")
{
    auto world = DirtyWorld();
    world_construct::InitDayClock(*world, 720, 0x40490fdbu);
    OBL_CHECK_EQ(world->m_gameStartTime, 3.14159274f);
    OBL_CHECK_EQ(world->m_gameDeltaTime, 0.0f);
    OBL_CHECK_EQ(world->m_gameDay, 0);
    OBL_CHECK_EQ(world->m_gameTime, 270.0f); // 720 / 24 * 9
    OBL_CHECK_EQ(world->m_gameDayTime, 9.0f);
    OBL_CHECK_EQ(world->m_previousDayTime, 9.0f);
    OBL_CHECK_EQ(world->m_lastGameDayTime, 9.0f);
    OBL_CHECK_EQ(world->m_nextStaffFeedbackTime, 360.0f); // three hours later
}

OBL_TEST_CASE("world_construct.count_unbuilt_skips_other_category_and_built_and_duplicates")
{
    auto world = DirtyWorld();
    FakeEnv env;
    env.count = 5;
    env.category = {0, 1, 3, 2, 1};
    env.objects = {0, 2, 0, 0, 0};
    world->m_numValidBuildings = 5;
    const int valid[] = {0, 1, 2, 3, 3};
    std::memcpy(world->m_validBuildingIDs, valid, sizeof(valid));
    world_construct::CountUnbuilt(*world, env);
    // 0: unbuilt, 1: built, 2: category 3, 3: unbuilt (listed twice, counted once), 4: not allowed
    OBL_CHECK_EQ(world->m_unbuiltAfterStartup, 2);
    OBL_CHECK_EQ(world->m_startup, 0);
}

OBL_TEST_CASE("world_construct.finish_fields_zeroes_the_tallies_only")
{
    auto world = DirtyWorld();
    ui::ViewState view;
    DirtyView(view);
    world_construct::FinishFields(*world, view, 2);
    OBL_CHECK_EQ(view.m_zoomRequest, 2);
    OBL_CHECK_EQ(world->m_totalGuestArrive[0], 0);
    OBL_CHECK_EQ(world->m_todayGuestUnhappyDepart[1], 0);
    OBL_CHECK_EQ(world->m_incomeToday, 0.0f);
    OBL_CHECK_EQ(world->m_averageIncome, 0.0f);
    OBL_CHECK_EQ(world->m_buildingsConstructed, 0);
    OBL_CHECK_EQ(world->m_guestsEaten, 0);
    OBL_CHECK_EQ(world->m_loadingSave, 0);
    OBL_CHECK_EQ(world->m_unbuiltAfterStartup, static_cast<int>(0xA5A5A5A5));
}
