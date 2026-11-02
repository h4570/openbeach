#pragma once

#include "game/ui/view_state.h"
#include "game/world/world_state.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

// ConstructWorld, step by step. This header holds the steps
// that touch only the fields of the World itself, in the order below; the PIS reading, the subsystem construction and the script start
// follow in later steps. The function runs once per world, on freshly allocated (not zeroed) memory, so a step writes every field that
// ConstructWorld writes and none it leaves alone (the profit/income/cost records stay as the allocation had them).
//
// The reads of the game's globals stay behind the callers' arguments (`gameSpeed`, `frameStat`, `zoom`) and the building-definition queries
// behind an `Env` (WorldParams and ObjectManager), so the logic is unit-tested without the game.
namespace obl::world_construct {

// The float bit pattern of a global is copied, not converted.
inline void CopyBits(float& destination, uint32_t bits)
{
    std::memcpy(&destination, &bits, sizeof(bits));
}

// Step 1a, the first block of the constructor (before the PIS is opened): the loading-save flag, the start-up flag, the camera and clock
// fields (the view state too), the power sums, the script variables and the live counters. `gameSpeed` is the float at g_GameSpeedTable (1.0 once
// InitGameSpeedTable has run; the first entry of the table is copied into the World's speed field).
inline void InitFields(world::World& world, ui::ViewState& view, ui::Cursor* cursor, ui::Input* input, bool loadingSave, float gameSpeed)
{
    view = {}; // the World's value-initialization used to zero these, m_zoomRequest included, until FinishFields
    world.m_loadingSave = loadingSave ? 1 : 0;
    world.m_startup = 1;
    view.m_devToggle = 0;
    view.m_cameraDragging = 0;
    world.m_gameStartTime = 0;
    world.m_lastGameDayTime = 0;
    world.m_gameTime = 0;
    world.m_gameDeltaTime = 0;
    world.m_powerUse = 0;
    world.m_maxPower = 0;
    world.m_currentPower = 0;
    world.m_powerDemand = 0;
    world.m_waterContam = 0;
    world.m_demoTime = 0;
    world.m_demoTimeSet = 0;
    world.m_gameSpeedLevel = 0;
    world.m_gameSpeed = gameSpeed;
    world.m_unbuiltAfterStartup = 0;
    world.m_scriptFlags = 0;
    world.m_paused = 0;
    world.m_forcedPaused = 0;
    view.m_drawObjectsArg = 0;
    world.m_numCircuitPoints[0] = 0;
    world.m_numCircuitPoints[1] = 0;
    world.m_numGuests = 0;
    world.m_numLitter = 0;
    world.m_numBuildings = 0;
    world.m_scenicVal = 0;
    view.m_darkenByDepth = 0;
    view.m_hotkeysDisabled = 0;
    std::memset(world.m_scriptVars, 0, sizeof(world.m_scriptVars));
    view.m_cursor = cursor;
    view.m_input = input;
}

// Step 1b, the clock: the game starts at 9:00 of day 0, `dayLength` = gameparameters.pis iDAY_LENGTH_IN_SECONDS (real seconds per game day).
// `frameStat` is the frame-time value of the frame statistics, copied into the World as bits. The arithmetic runs in extended
// precision before the multiply (`24.0 / (float)length`), hence the double here.
inline void InitDayClock(world::World& world, int dayLength, uint32_t frameStat)
{
    CopyBits(world.m_gameStartTime, frameStat);
    world.m_gameDeltaTime = 0;
    const float secondsPerHour = static_cast<float>(dayLength) * 0.041666668f; // 1/24 as the game's float constant
    world.m_gameTime = static_cast<float>(static_cast<double>(secondsPerHour) * 9.0);
    world.m_gameDay = 0;
    const float hour = static_cast<float>((24.0 / static_cast<double>(dayLength)) * static_cast<double>(world.m_gameTime));
    world.m_gameDayTime = hour;
    world.m_previousDayTime = hour;
    world.m_lastGameDayTime = world.m_gameDayTime;
    world.m_nextStaffFeedbackTime = static_cast<float>(static_cast<double>(secondsPerHour) * 3.0 + static_cast<double>(world.m_gameTime));
}

// Once the scenario script has started, count allowed buildings that have no placed instance, excluding the Other category.
// The environment supplies catalog counts, categories and placed-object counts by building key. Clear the startup flag first.
template<class Env> void CountUnbuilt(world::World& world, Env& env)
{
    world.m_startup = 0;
    world.m_unbuiltAfterStartup = 0;
    const uint32_t count = static_cast<uint32_t>(env.BuildingCount());
    for (uint32_t id = 0; id < count; ++id) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(world.m_numValidBuildings); ++i) {
            if (static_cast<uint32_t>(world.m_validBuildingIDs[i]) != id) continue;
            if (env.Category(static_cast<int>(id)) != 3 && env.ObjectsOf(static_cast<int>(id)) == 0) ++world.m_unbuiltAfterStartup;
            break; // the first match decides, a duplicate id is not counted twice
        }
    }
}

// Step 1d, the last block: the zoom level of the renderer (`zoom`), the guest counters, the day's
// money and the statistics, the building/guest tallies and the cleared loading-save flag.
inline void FinishFields(world::World& world, ui::ViewState& view, int zoom)
{
    view.m_zoomRequest = zoom;
    for (int* pair : {world.m_totalGuestArrive, world.m_totalGuestDepart, world.m_todayGuestArrive, world.m_todayGuestDepart,
                 world.m_totalGuestUnhappyDepart, world.m_todayGuestUnhappyDepart})
        pair[0] = pair[1] = 0;
    for (float* money : {&world.m_incomeToday, &world.m_demolishIncomeToday, &world.m_fineIncomeToday, &world.m_accomIncomeToday,
                 &world.m_attractIncomeToday, &world.m_retailIncomeToday, &world.m_totalFines, &world.m_costsToday,
                 &world.m_constructCostToday, &world.m_sceneryCostToday, &world.m_wagesCostToday, &world.m_stockCostToday,
                 &world.m_averageProfit, &world.m_averageCost, &world.m_averageIncome})
        *money = 0;
    for (int* tally :
            {&world.m_buildingsConstructed, &world.m_diffBuildingsConstructed, &world.m_barsConstructed, &world.m_buildingsLeftToBuild,
                    &world.m_partyCount, &world.m_shagCount, &world.m_pissedCount, &world.m_guestsDrowned, &world.m_guestsEaten})
        *tally = 0;
    world.m_averageTotty = world.m_averageBeauty = world.m_averageSafety = 0;
    world.m_loadingSave = 0;
}

} // namespace obl::world_construct
