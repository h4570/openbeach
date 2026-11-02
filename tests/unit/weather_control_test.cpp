#include "game/weather/weather_state.h"
#include "unit_test.h"

#include "game/weather/weather_control.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::weather;
using weather::Weather;

namespace {

std::unique_ptr<Weather> NewWeather()
{
    auto weather = std::make_unique<Weather>();
    return weather;
}

struct FakeBuildings {
    std::vector<std::string> keys;
    std::vector<int32_t> ids;
    std::vector<int> draws; // what Rand() returns, in order
    size_t drawn = 0;

    uint32_t BuildingCount() { return static_cast<uint32_t>(keys.size()); }
    bool KeyEquals(uint32_t i, const char* key) { return keys[i] == key; }
    int32_t BuildingId(uint32_t i) { return ids[i]; }
    int Rand() { return draws[drawn++]; }
};

} // namespace

OBL_TEST_CASE("float_to_int.truncates_to_64_bits_and_keeps_the_low_dword")
{
    OBL_CHECK_EQ(FloatToIntWrap(0.0), 0);
    OBL_CHECK_EQ(FloatToIntWrap(254.99), 254);
    OBL_CHECK_EQ(FloatToIntWrap(-0.99), 0);
    OBL_CHECK_EQ(FloatToIntWrap(-1.5), -1);
    OBL_CHECK_EQ(FloatToIntWrap(4294967296.0 + 7.0), 7); // beyond int: wraps, does not saturate
    OBL_CHECK_EQ(FloatToIntWrap(2147483648.0), static_cast<int>(0x80000000u));
    OBL_CHECK_EQ(FloatToIntWrap(std::numeric_limits<double>::quiet_NaN()), 0);
    OBL_CHECK_EQ(FloatToIntWrap(1.0e30), 0); // beyond 64 bits: the integer indefinite, whose low 32 bits are 0
    OBL_CHECK_EQ(FloatToIntWrap(std::numeric_limits<double>::infinity()), 0);
}

OBL_TEST_CASE("weather_control.set_override_changes_one_channel")
{
    auto weather = NewWeather();
    weather->m_manualColor = 0x00112233;
    SetOverrideGreen(*weather, 1.0f);
    OBL_CHECK_EQ(weather->m_manualColor, 0x0011ff33u);
    SetOverrideBlue(*weather, 0.0f);
    OBL_CHECK_EQ(weather->m_manualColor, 0x0011ff00u);
    SetOverrideRed(*weather, 0.5f);
    OBL_CHECK_EQ(weather->m_manualColor, 0x007fff00u); // 127.5 truncates to 127
}

OBL_TEST_CASE("weather_control.set_override_rebuilds_the_colour_without_the_top_byte")
{
    auto weather = NewWeather();
    weather->m_manualColor = 0xaa112233u;
    SetOverrideRed(*weather, 1.0f);
    OBL_CHECK_EQ(weather->m_manualColor, 0x00ff2233u);
    weather->m_manualColor = 0xaa112233u;
    SetOverrideGreen(*weather, 0.0f);
    OBL_CHECK_EQ(weather->m_manualColor, 0x00110033u); // the mask 0xff00ff drops the top byte too
    weather->m_manualColor = 0xaa112233u;
    SetOverrideBlue(*weather, 0.0f);
    OBL_CHECK_EQ(weather->m_manualColor, 0x00112200u);
}

OBL_TEST_CASE("weather_control.set_override_wraps_out_of_range_values_through_the_low_byte")
{
    auto weather = NewWeather();
    SetOverrideBlue(*weather, 2.0f); // 510 -> 0xfe
    OBL_CHECK_EQ(weather->m_manualColor, 0xfeu);
    SetOverrideBlue(*weather, -0.001f); // truncates to 0
    OBL_CHECK_EQ(weather->m_manualColor, 0u);
    SetOverrideBlue(*weather, -1.0f); // -255 -> 0x01
    OBL_CHECK_EQ(weather->m_manualColor, 1u);
    SetOverrideBlue(*weather, std::numeric_limits<float>::quiet_NaN());
    OBL_CHECK_EQ(weather->m_manualColor, 0u);
    SetOverrideBlue(*weather, 1.0e30f); // beyond 64 bits
    OBL_CHECK_EQ(weather->m_manualColor, 0u);
}

OBL_TEST_CASE("weather_control.get_override_is_the_exact_product_of_the_byte_and_the_float_step")
{
    auto weather = NewWeather();
    weather->m_manualColor = 0x01ff8000u; // the top byte is not a channel
    OBL_CHECK_EQ(GetOverrideRed(*weather), 255.0 * static_cast<double>(kColorStep));
    OBL_CHECK_EQ(GetOverrideGreen(*weather), 128.0 * static_cast<double>(kColorStep));
    OBL_CHECK_EQ(GetOverrideBlue(*weather), 0.0);
    // the float step is slightly above 1/255, so a full channel is a hair above 1 and the callers' x255 truncation gives 255
    OBL_CHECK(GetOverrideRed(*weather) > 1.0);
    OBL_CHECK(GetOverrideRed(*weather) * 255.0 >= 255.0);
}

OBL_TEST_CASE("weather_control.thunder_level_from_off_schedules_the_first_strike")
{
    auto weather = NewWeather();
    SetThunderLevel(*weather, 7, 100.0f);
    OBL_CHECK_EQ(weather->m_strikeLevel, 7);
    OBL_CHECK_EQ(weather->m_strikes, 1);
    OBL_CHECK_EQ(weather->m_nextStrikeTime, static_cast<float>(3.0 * 0.699999988079071 + 100.0 + 3.0));
    SetThunderLevel(*weather, 10, 0.0f); // still on: no new schedule, the pending strike is switched off
    OBL_CHECK_EQ(weather->m_strikeLevel, 10);
    OBL_CHECK_EQ(weather->m_strikes, 0);
    OBL_CHECK_EQ(weather->m_nextStrikeTime, static_cast<float>(3.0 * 0.699999988079071 + 100.0 + 3.0));
    SetThunderLevel(*weather, 0, 5.0f); // stop
    OBL_CHECK_EQ(weather->m_strikeLevel, 0);
    OBL_CHECK_EQ(weather->m_strikes, 0);
}

OBL_TEST_CASE("weather_control.thunder_level_zero_from_off_still_arms_a_strike_ten_steps_ahead")
{
    auto weather = NewWeather();
    SetThunderLevel(*weather, 0, 1.0f);
    OBL_CHECK_EQ(weather->m_strikeLevel, 0);
    OBL_CHECK_EQ(weather->m_strikes, 1);
    OBL_CHECK_EQ(weather->m_nextStrikeTime, static_cast<float>(10.0 * 0.699999988079071 + 1.0 + 3.0));
}

OBL_TEST_CASE("weather_control.lightning_area_stores_the_four_values_in_order")
{
    auto weather = NewWeather();
    SetLightningArea(*weather, 1, 2, 3, 4);
    OBL_CHECK_EQ(weather->m_strikeArea.left, 1);
    OBL_CHECK_EQ(weather->m_strikeArea.top, 2);
    OBL_CHECK_EQ(weather->m_strikeArea.right, 3);
    OBL_CHECK_EQ(weather->m_strikeArea.bottom, 4);
}

OBL_TEST_CASE("weather_control.target_search_walks_the_buildings_backwards_and_draws_once")
{
    auto weather = NewWeather();
    FakeBuildings env;
    env.keys = {"TOWER", "BAR", "TOWER", "HOTEL", "TOWER"};
    env.ids = {10, 11, 12, 13, 14};
    env.draws = {1};
    SetLightningTargetByName(*weather, "TOWER", env);
    OBL_CHECK_EQ(env.drawn, 1u);
    // matches in walking order: 14, 12, 10; the draw 1 % 3 picks 12
    OBL_CHECK_EQ(weather->m_strikeBuildingId, 12);
    OBL_CHECK_EQ(weather->m_strikes, 1);
    OBL_CHECK_EQ(weather->m_hasStrikeTarget, 1);
}

OBL_TEST_CASE("weather_control.target_search_reduces_the_draw_unsigned")
{
    auto weather = NewWeather();
    FakeBuildings env;
    env.keys = {"A", "A", "A"};
    env.ids = {1, 2, 3};
    env.draws = {-1}; // 0xffffffff % 3 == 0 -> the first match in walking order, id 3
    SetLightningTargetByName(*weather, "A", env);
    OBL_CHECK_EQ(weather->m_strikeBuildingId, 3);
}

OBL_TEST_CASE("weather_control.target_search_without_a_match_changes_nothing_and_draws_nothing")
{
    auto weather = NewWeather();
    weather->m_strikeBuildingId = 77;
    FakeBuildings env;
    env.keys = {"TOWER", "BAR"};
    env.ids = {10, 11};
    SetLightningTargetByName(*weather, "tower", env); // the compare is case-sensitive
    OBL_CHECK_EQ(env.drawn, 0u);
    OBL_CHECK_EQ(weather->m_strikeBuildingId, 77);
    OBL_CHECK_EQ(weather->m_strikes, 0);
    OBL_CHECK_EQ(weather->m_hasStrikeTarget, 0);
    FakeBuildings none;
    SetLightningTargetByName(*weather, "TOWER", none);
    OBL_CHECK_EQ(weather->m_strikes, 0);
}

OBL_TEST_CASE("weather_control.target_search_keeps_the_first_hundred_matches")
{
    auto weather = NewWeather();
    FakeBuildings env;
    for (int i = 0; i < 150; ++i) {
        env.keys.push_back("B");
        env.ids.push_back(1000 + i);
    }
    env.draws = {7};
    SetLightningTargetByName(*weather, "B", env);
    OBL_CHECK_EQ(weather->m_strikeBuildingId, 1000 + 149 - 7); // walking order: 149, 148, ...
}

OBL_TEST_CASE("weather_control.current_modifiers_point_at_the_struct")
{
    auto weather = NewWeather();
    OBL_CHECK(CurrentModifiers(*weather) == weather->m_desireModifiers);
}
