#pragma once

#include "game/world/world_state.h"

#include <cstdint>

// The World block of the save: its field table, which
// the obl.save reader and writer and the world snapshot walk. The names are the obl.save keys (world_save_json.h drops the few the file does not hold).
namespace obl::world_save {

constexpr int kMapIdSize = 0x100;

// Whether a record is part of the world snapshot too. The boat list (raw pointers) and the 128-day histories (uninitialized future days) are
// written to the save only.
enum class Scope { Both, SaveOnly };

// The field table: every record of WriteWorldJson after the class name, in save order. `W` is `world::World` or `const world::World`, `S` is
// `uint32_t` or `const uint32_t` (the script state, which lives in the WorldScript, not in the World). The list exists once here: the obl.save
// reader and writer and the world snapshot (EmitWorldFields) are visitors. A visitor provides:
//   String(name, char (&)[kMapIdSize])      the map id
//   Int/UInt/Float/Bool(name, field)          one record (Bool fields are uint8_t)
//   Ints(name, int*, count, Scope)            one array record
//   Floats(name, float*, count, Scope)        one array record (only the histories, all SaveOnly)
//   ScriptState(name, S&)                     the "int" record of the script state, the last one
// The four objective flags are four UInt records under one name.
template<class W, class S, class Visitor> void VisitFields(W& world, S& scriptState, Visitor& v)
{
    v.String("map_id", world.m_mapId);
    v.Float("game_start_time", world.m_gameStartTime);
    v.Float("game_time", world.m_gameTime);
    v.Int("game_day", world.m_gameDay);
    v.Float("day_time", world.m_gameDayTime);
    v.Float("last_day_time", world.m_previousDayTime);
    v.Float("previous_day_time", world.m_lastGameDayTime);
    v.Float("delta_time", world.m_gameDeltaTime);
    v.Float("speed", world.m_gameSpeed);
    v.Int("speed_level", world.m_gameSpeedLevel);
    v.Float("next_staff_feedback_time", world.m_nextStaffFeedbackTime);
    v.Bool("startup", world.m_startup);
    v.Int("available_building_count", world.m_numValidBuildings);
    v.Ints("available_buildings", world.m_validBuildingIDs, 0x80, Scope::Both);
    v.Bool("breakdowns_allowed", world.m_breakDownsAllowed);
    v.Bool("litter_allowed", world.m_allowLitter);
    v.Ints("boat_list", world.m_boatList, 5, Scope::SaveOnly);
    v.Int("boat_count", world.m_numBoats);
    v.Float("power_use", world.m_powerUse);
    v.Float("max_power", world.m_maxPower);
    v.Float("current_power", world.m_currentPower);
    v.Float("power_demand", world.m_powerDemand);
    v.Int("script_flags", world.m_scriptFlags);
    v.Ints("script_variables", world.m_scriptVars, 0x20, Scope::Both);
    v.Bool("paused", world.m_paused);
    v.Bool("forced_paused", world.m_forcedPaused);
    v.Int("guests", world.m_numGuests);
    v.Int("litter", world.m_numLitter);
    v.Int("buildings", world.m_numBuildings);
    v.Float("scenic_value", world.m_scenicVal);
    v.Float("water_contamination", world.m_waterContam);
    for (auto& status : world.m_objectiveStatus) v.UInt("objective_status", status);
    v.Ints("total_guest_arrivals", world.m_totalGuestArrive, 2, Scope::Both);
    v.Ints("total_guest_departures", world.m_totalGuestDepart, 2, Scope::Both);
    v.Ints("today_guest_arrivals", world.m_todayGuestArrive, 2, Scope::Both);
    v.Ints("today_guest_departures", world.m_todayGuestDepart, 2, Scope::Both);
    v.Ints("total_unhappy_departures", world.m_totalGuestUnhappyDepart, 2, Scope::Both);
    v.Ints("today_unhappy_departures", world.m_todayGuestUnhappyDepart, 2, Scope::Both);
    v.Float("income_today", world.m_incomeToday);
    v.Float("demolition_income_today", world.m_demolishIncomeToday);
    v.Float("fine_income_today", world.m_fineIncomeToday);
    v.Float("accommodation_income_today", world.m_accomIncomeToday);
    v.Float("attraction_income_today", world.m_attractIncomeToday);
    v.Float("retail_income_today", world.m_retailIncomeToday);
    v.Float("total_fines", world.m_totalFines);
    v.Float("costs_today", world.m_costsToday);
    v.Float("construction_cost_today", world.m_constructCostToday);
    v.Float("scenery_cost_today", world.m_sceneryCostToday);
    v.Float("wages_cost_today", world.m_wagesCostToday);
    v.Float("stock_cost_today", world.m_stockCostToday);
    v.Float("average_profit", world.m_averageProfit);
    v.Float("average_cost", world.m_averageCost);
    v.Float("average_income", world.m_averageIncome);
    v.Floats("profit_history", world.m_profitRecord, 0x80, Scope::SaveOnly);
    v.Floats("income_history", world.m_incomeRecord, 0x80, Scope::SaveOnly);
    v.Floats("cost_history", world.m_costRecord, 0x80, Scope::SaveOnly);
    v.Int("buildings_constructed", world.m_buildingsConstructed);
    v.Int("different_buildings_constructed", world.m_diffBuildingsConstructed);
    v.Int("bars_constructed", world.m_barsConstructed);
    v.Int("buildings_left_to_build", world.m_buildingsLeftToBuild);
    v.Float("average_attractiveness", world.m_averageTotty);
    v.Float("average_beauty", world.m_averageBeauty);
    v.Float("average_safety", world.m_averageSafety);
    v.Int("party_count", world.m_partyCount);
    v.Int("romance_count", world.m_shagCount);
    v.Int("drunk_count", world.m_pissedCount);
    v.Int("guests_drowned", world.m_guestsDrowned);
    v.Int("guests_eaten", world.m_guestsEaten);
    v.Int("unbuilt_after_startup", world.m_unbuiltAfterStartup);
    v.ScriptState("script_state", scriptState);
}

// DestroyWorld is world::World's destructor (game/world/world_state.h); the subsystem order lives there.
using world::Subsystem;

} // namespace obl::world_save
