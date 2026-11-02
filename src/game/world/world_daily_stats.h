#pragma once

#include "game/world/world_state.h"

#include <algorithm>
#include <cstdint>

// world::RollDailyStats: the day change of the profit/cost/income history.
// It writes only the three 128-day records, the three averages and the "today" counters; it reads the day number.
// No RNG, no messages, no allocation.
//
// Quirk kept on purpose: the "shift by one" loops copy forward (`rec[i] = rec[i - 1]` for i = 1..127 in ascending order), so
// every entry from 1 up ends as the value rec[0] had before the loop, instead of the previous day. The history therefore holds yesterday's value 127 times and
// the new value once, and the averages (sums over min(day, 128) records from rec[0]) are skewed accordingly.
//
// Arithmetic as in double at 53-bit precision: the profit is the double difference of two floats rounded to float on
// the store, the sums are double, the quotient divides by the day as an unsigned 64-bit integer and is rounded to float on the store.
namespace obl {

inline void RollDailyStats(world::World& world)
{
    constexpr int kDays = 0x80;
    auto smear = [](float* record) {
        for (int i = 1; i < kDays; ++i) record[i] = record[i - 1];
    };
    // The records are summed from index 0 while fewer than `day` and fewer than 128 have been added (both unsigned compares).
    auto average = [&](const float* record, uint32_t day) {
        double sum = 0.0;
        for (uint32_t i = 0; i < day && i < static_cast<uint32_t>(kDays); ++i) sum += record[i];
        return static_cast<float>(sum / static_cast<double>(day));
    };
    const uint32_t day = static_cast<uint32_t>(world.m_gameDay);

    smear(world.m_profitRecord);
    const double profit = static_cast<double>(world.m_incomeToday) - static_cast<double>(world.m_costsToday);
    world.m_profitRecord[0] = static_cast<float>(profit);
    world.m_averageProfit = day == 0 ? static_cast<float>(profit) : average(world.m_profitRecord, day);

    smear(world.m_costRecord);
    world.m_costRecord[0] = world.m_costsToday;
    world.m_averageCost = day == 0 ? world.m_costsToday : average(world.m_costRecord, day);

    smear(world.m_incomeRecord);
    world.m_incomeRecord[0] = world.m_incomeToday;
    world.m_averageIncome = day == 0 ? world.m_incomeToday : average(world.m_incomeRecord, day);

    // Every "today" counter but m_totalFines, which keeps running over the whole game.
    world.m_incomeToday = 0.0f;
    world.m_demolishIncomeToday = 0.0f;
    world.m_fineIncomeToday = 0.0f;
    world.m_accomIncomeToday = 0.0f;
    world.m_attractIncomeToday = 0.0f;
    world.m_retailIncomeToday = 0.0f;
    world.m_costsToday = 0.0f;
    world.m_constructCostToday = 0.0f;
    world.m_sceneryCostToday = 0.0f;
    world.m_wagesCostToday = 0.0f;
    world.m_stockCostToday = 0.0f;
}

} // namespace obl
