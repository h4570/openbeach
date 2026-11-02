#include "game/ui/music_player.h"
#include "game/ui/sound_types.h"
#include "game/world/world_update.h"
#include "game/objects/building_api.h"
#include "game/objects/object_registry_api.h"

#include "game/game/game_accessors.h"
#include "game/game/rng.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/objects/object_manager_state.h"
#include "game/objects/object_building.h"
#include "game/world/world_state.h"
#include "game/objects/script_types.h"
#include "game/world/world_script_types.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "game/world/world_daily_stats.h"
#include "game/world/world_day_cycle.h"
#include "game/world/world_power.h"
#include "game/ferry/ferry.h"
#include "game/messages/message_handler.h"
#include "game/world/world_messages.h"
#include "game/objects/building_methods.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace obl::world {
namespace {

using layout::World;
using object::Building;

// ---- world::RollDailyStats --------------------------------------------------------------------------------------------

// Call counters (`call-report world.roll-daily-stats`).
test::CallCounter g_rollCalls("world.roll-daily-stats");

void RunDailyStatsEntry(World* world)
{
    g_rollCalls.Count();
    RollDailyStats(*world);
}

// `call-drive world.roll-daily-stats <rounds>`: a day change happens once per game day, so the driver feeds the function worlds
// with random records and counters (day 0, 1, below/at/above the 128-day window, huge) and restores the real World afterwards.
void DriveRoll(int rounds)
{
    World* world = game::GetWorld();
    if (!world) {
        OBL_LOG("autotest: FAILED call-drive world.roll-daily-stats: no world");
        return;
    }
    std::vector<uint8_t> saved(sizeof(World));
    std::memcpy(saved.data(), world, sizeof(World));

    uint32_t seed = 0x2468ace;
    auto next = [&seed]() { return (seed = seed * 1664525u + 1013904223u) >> 8; };
    auto real = [&](int style) {
        switch (style) {
        case 0: return static_cast<float>(next() % 20001) - 10000.0f; // money, either sign
        case 1: return static_cast<float>(next() & 0xffffff) / 7.0f; // fractions that need float rounding
        case 2: return 0.0f;
        case 3: return std::numeric_limits<float>::quiet_NaN();
        default: return static_cast<float>(next() & 0xff) * 1e-3f;
        }
    };
    static const uint32_t days[] = {0, 1, 2, 3, 7, 100, 127, 128, 129, 200, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    for (int round = 0; round < rounds; ++round) {
        for (uint32_t day : days) {
            world->m_gameDay = static_cast<int>(day);
            const int style = static_cast<int>(next() % (round % 4 == 3 ? 5 : 2));
            for (float* record : {world->m_profitRecord, world->m_incomeRecord, world->m_costRecord})
                for (int i = 0; i < 0x80; ++i) record[i] = real(style);
            float* today[] = {&world->m_incomeToday, &world->m_demolishIncomeToday, &world->m_fineIncomeToday, &world->m_accomIncomeToday,
                    &world->m_attractIncomeToday, &world->m_retailIncomeToday, &world->m_totalFines, &world->m_costsToday,
                    &world->m_constructCostToday, &world->m_sceneryCostToday, &world->m_wagesCostToday, &world->m_stockCostToday,
                    &world->m_averageProfit, &world->m_averageCost, &world->m_averageIncome};
            for (float* value : today) *value = real(style == 3 ? 3 : static_cast<int>(next() % 2));
            RunDailyStatsEntry(world);
        }
    }
    std::memcpy(world, saved.data(), sizeof(World));
}

// ---- world::UpdateBuildingTotals / world::SwitchOffConsumers ----------------------------------------------------------------------

using BuildingJobTest = bool (*)(const object::Building*);
const BuildingJobTest kHasBuildJob{&object::building_api::HasBuildJob};
const BuildingJobTest kHasDemolishJob{&object::building_api::HasDemolishJob};

test::CallCounter g_totalsCalls("world.update-building-totals");
test::CallCounter g_shedCalls("world.shed-power-load");

// The game side of the Env contract in src/game/world/world_power.h: the building list of the ObjectManager, definitions and the game's own
// accessors for the job and output queries, BuildingShedPower for the shedding itself (it removes staff and posts messages).
class PowerEnv {
public:
    explicit PowerEnv(object::Base** list) : m_list(list) {}

    int Power(uint32_t i) { return Building(i)->m_definition->m_Power; }
    bool ClosedForPower(uint32_t i) { return Building(i)->m_closedForPower != 0; }
    bool HasBuildJob(uint32_t i) { return kHasBuildJob(Building(i)); }
    bool HasDemolishJob(uint32_t i) { return kHasDemolishJob(Building(i)); }
    int OutputPower(uint32_t i) { return object::BuildingGetPowerOutput(Building(i)); }
    int Rand() { return game::Rand(); }
    BuildingBehavior Behavior(uint32_t i) { return Building(i)->m_definition->m_Behavior; }
    void SwitchOffConsumer(uint32_t i) { object::BuildingShedPower(Building(i)); }

protected:
    object::Building* Building(uint32_t i) { return static_cast<object::Building*>(m_list[i]); }

private:
    object::Base** m_list;
};

world_power::Sums ReadSums(const World& world)
{
    world_power::Sums sums;
    sums.total = world.m_powerUse;
    sums.installed = world.m_maxPower;
    sums.output = world.m_currentPower;
    sums.active = world.m_powerDemand;
    return sums;
}

void WriteSums(World& world, const world_power::Sums& sums)
{
    world.m_powerUse = sums.total;
    world.m_maxPower = sums.installed;
    world.m_currentPower = sums.output;
    world.m_powerDemand = sums.active;
}

void SwitchOffConsumers(World* world, object::Base** list, uint32_t count)
{
    g_shedCalls.Count();
    PowerEnv env(list);
    world_power::SwitchOffConsumers(ReadSums(*world), count, env);
}

void UpdateBuildingTotalsEntry(World* world)
{
    object::Manager* objects = world->m_objectManager;
    g_totalsCalls.Count();
    PowerEnv env(objects->m_buildings.data());
    WriteSums(*world, world_power::SumPower(env, objects->m_buildings.size()));
    SwitchOffConsumers(world, objects->m_buildings.data(), static_cast<uint32_t>(objects->m_buildings.size()));
}

// ---- world::UpdateDayCycle -------------------------------------------------------------------------------------------------

using MessageHandler = messages::Handler;

test::CallCounter g_dayCalls("world.update-day-cycle");

audio::MusicPlayer* GetMusicPlayer()
{
    auto* gui = game::GetGuiManager();
    return gui ? static_cast<audio::MusicPlayer*>(gui->m_musicPlayer) : nullptr;
}

// The game side of the Env contract in src/game/world/world_day_cycle.h. The sending loops read the list and its length
// once, the receiver is stored into the shared message before every Post (the world-wide cases go through world::PostMessageToClass, the
// function, which does the same per object of one class).
class DayEnv {
public:
    explicit DayEnv(World& world) : m_world(world) {}

    void PostToBuildings(messages::Message& message)
    {
        const auto list = object::object_manager::GetListByClass(game::GetObjectManager(), day_cycle::kBuildingClass);
        const auto count = static_cast<uint32_t>(list.size());
        for (uint32_t i = 0; i < count; ++i) {
            message.receiver = static_cast<int>(list[i]->m_id);
            messages::message_facade::Post(m_world.m_messageHandler, &message);
        }
    }
    void PostToClass(int cls, messages::Message& message) { world::PostMessageToClass(m_world.m_messageHandler, cls, message); }
    void PostToStaff(messages::Message& message)
    {
        const auto list = object::object_manager::GetNonServiceStaffList(game::GetObjectManager());
        const auto count = static_cast<uint32_t>(list.size());
        for (uint32_t i = 0; i < count; ++i) {
            message.receiver = static_cast<int>(list[i]->m_id);
            messages::message_facade::Post(m_world.m_messageHandler, &message);
        }
    }
    void ResetFerryCursor() { ferry::ferry_facade::ResetCursor(m_world.m_ferryManager); }
    // Through the entry point, so it counts like any other call.
    void RollDailyStats() { RunDailyStatsEntry(&m_world); }
    void SetNight(bool night) { GetMusicPlayer()->SetNight(night ? 1 : 0); }

private:
    World& m_world;
};

void UpdateDayCycleEntry(World* world)
{
    g_dayCalls.Count();
    DayEnv env(*world);
    day_cycle::UpdateDayCycle(*world, env);
}

} // namespace

void UpdateBuildingTotals(World& world)
{
    UpdateBuildingTotalsEntry(&world);
}
void UpdateDayCycle(World& world)
{
    UpdateDayCycleEntry(&world);
}

void UpdateBuildingTotalsDirect(void* world)
{
    UpdateBuildingTotalsEntry(static_cast<World*>(world));
}

bool RegisterWorldUpdateDriver()
{
    g_rollCalls.SetDriver(&DriveRoll);
    return true;
}

} // namespace obl::world
