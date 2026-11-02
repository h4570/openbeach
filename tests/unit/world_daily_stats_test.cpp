#include "unit_test.h"
#include "game/world/world_state.h"

#include "game/world/world_daily_stats.h"

#include <cmath>
#include <cstring>
#include <memory>

using namespace obl;

namespace {

std::unique_ptr<layout::World> NewWorld()
{
    auto world = std::make_unique<layout::World>();
    return world;
}

} // namespace

OBL_TEST_CASE("world_daily_stats.day_zero_has_no_average_division")
{
    auto world = NewWorld();
    world->m_gameDay = 0;
    world->m_incomeToday = 300.0f;
    world->m_costsToday = 120.5f;
    RollDailyStats(*world);
    OBL_CHECK_EQ(world->m_profitRecord[0], 179.5f);
    OBL_CHECK_EQ(world->m_averageProfit, 179.5f);
    OBL_CHECK_EQ(world->m_averageCost, 120.5f);
    OBL_CHECK_EQ(world->m_averageIncome, 300.0f);
}

OBL_TEST_CASE("world_daily_stats.shift_copies_forward_one_day")
{
    // rec[i] = rec[i - 1] in ascending order smears the old rec[0] over the whole history.
    auto world = NewWorld();
    world->m_gameDay = 3;
    for (int i = 0; i < 0x80; ++i) world->m_incomeRecord[i] = static_cast<float>(1000 + i);
    world->m_incomeToday = 7.0f;
    RollDailyStats(*world);
    OBL_CHECK_EQ(world->m_incomeRecord[0], 7.0f);
    for (int i = 1; i < 0x80; ++i) OBL_CHECK_EQ(world->m_incomeRecord[i], 1000.0f);
    // Three days: the new value and two copies of yesterday's rec[0].
    OBL_CHECK_EQ(world->m_averageIncome, static_cast<float>((7.0 + 1000.0 + 1000.0) / 3.0));
}

OBL_TEST_CASE("world_daily_stats.average_window_is_128_days_divided_by_the_day")
{
    auto world = NewWorld();
    world->m_gameDay = 200;
    for (int i = 0; i < 0x80; ++i) world->m_costRecord[i] = 2.0f;
    world->m_costsToday = 10.0f;
    RollDailyStats(*world);
    // 128 records (10 + 127 x 2) are summed but the quotient uses the day number, not the record count.
    OBL_CHECK_EQ(world->m_averageCost, static_cast<float>((10.0 + 127 * 2.0) / 200.0));
}

OBL_TEST_CASE("world_daily_stats.profit_is_income_minus_costs_and_may_be_negative")
{
    auto world = NewWorld();
    world->m_gameDay = 1;
    world->m_incomeToday = 16777216.0f;
    world->m_costsToday = 0.1f;
    RollDailyStats(*world);
    OBL_CHECK_EQ(world->m_profitRecord[0], static_cast<float>(static_cast<double>(16777216.0f) - static_cast<double>(0.1f)));
    world->m_incomeToday = 5.0f;
    world->m_costsToday = 8.0f;
    RollDailyStats(*world);
    OBL_CHECK_EQ(world->m_profitRecord[0], -3.0f);
}

OBL_TEST_CASE("world_daily_stats.today_counters_clear_but_not_the_fines")
{
    auto world = NewWorld();
    world->m_gameDay = 5;
    world->m_incomeToday = world->m_demolishIncomeToday = world->m_fineIncomeToday = 1.0f;
    world->m_accomIncomeToday = world->m_attractIncomeToday = world->m_retailIncomeToday = 2.0f;
    world->m_costsToday = world->m_constructCostToday = world->m_sceneryCostToday = 3.0f;
    world->m_wagesCostToday = world->m_stockCostToday = 4.0f;
    world->m_totalFines = 99.0f;
    RollDailyStats(*world);
    for (float value : {world->m_incomeToday, world->m_demolishIncomeToday, world->m_fineIncomeToday, world->m_accomIncomeToday,
                 world->m_attractIncomeToday, world->m_retailIncomeToday, world->m_costsToday, world->m_constructCostToday,
                 world->m_sceneryCostToday, world->m_wagesCostToday, world->m_stockCostToday})
        OBL_CHECK_EQ(value, 0.0f);
    OBL_CHECK_EQ(world->m_totalFines, 99.0f);
}

OBL_TEST_CASE("world_daily_stats.huge_day_number_is_unsigned")
{
    auto world = NewWorld();
    world->m_gameDay = static_cast<int>(0x80000000u);
    world->m_incomeToday = 50.0f;
    RollDailyStats(*world);
    // The loop is bounded by the 128 records, the divisor is the unsigned day (2^31), not a negative number.
    OBL_CHECK(world->m_averageIncome > 0.0f);
    OBL_CHECK(std::isfinite(world->m_averageIncome));
}
