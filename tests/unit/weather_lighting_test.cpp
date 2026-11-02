#include "game/weather/weather_state.h"
#include "unit_test.h"

#include "fake_strike_env.h"
#include "game/weather/weather_lighting.h"

#include <cstring>
#include <memory>

using namespace obl;
using namespace obl::weather;
using weather::Weather;

namespace {

struct FakeEnv : unit::FakeStrikeEnv {
    int draws = 0, releases = 0;
    void DrawLightning() { ++draws; }
    void ReleaseLightmap(void*) { ++releases; }
};

// The constructor's daylight: key i at hour i, every key the same colour here; a normal day (weather 1) with no tint.
std::unique_ptr<Weather> NewWeather(uint32_t keyColor = 0x40'40'40)
{
    auto weather = std::make_unique<Weather>();
    for (uint32_t i = 0; i < 24; ++i) {
        weather->m_lightKeys[i].m_hour = static_cast<float>(i);
        weather->m_lightKeys[i].m_color = keyColor;
    }
    weather->m_lightIndex = 0;
    weather->m_previousLightIndex = 23;
    weather->m_currentState = weather->m_previousState = 1;
    return weather;
}

int RedStep(const Weather& weather, int table, int i, bool rgb555)
{
    return (weather.m_toneTables[table][i] >> (rgb555 ? 10 : 11)) & 31;
}
int GreenStep(const Weather& weather, int table, int i)
{
    return (weather.m_toneTables[table][0x20 + i] >> 5) & 63;
}
int BlueStep(const Weather& weather, int table, int i, bool rgb555)
{
    return weather.m_toneTables[table][(rgb555 ? 0x40 : 0x60) + i];
}

} // namespace

OBL_TEST_CASE("weather_lighting.key_pair_follows_the_hour")
{
    auto weather = NewWeather();
    lighting::AdvanceLightIndex(*weather, 5.5f);
    OBL_CHECK_EQ(weather->m_lightIndex, 6u);
    OBL_CHECK_EQ(weather->m_previousLightIndex, 5u);
    lighting::AdvanceLightIndex(*weather, 23.5f); // past the last key: the wrap key 0 only counts before noon
    OBL_CHECK_EQ(weather->m_lightIndex, 0u);
    OBL_CHECK_EQ(weather->m_previousLightIndex, 23u);
    lighting::AdvanceLightIndex(*weather, 0.5f); // after midnight key 0 is behind the hour
    OBL_CHECK_EQ(weather->m_lightIndex, 1u);
    OBL_CHECK_EQ(weather->m_previousLightIndex, 0u);
}

OBL_TEST_CASE("weather_lighting.daylight_is_interpolated_per_channel_and_truncated")
{
    auto weather = NewWeather(0xffffff);
    weather->m_lightKeys[5].m_color = 0x000000;
    weather->m_lightIndex = 6;
    weather->m_previousLightIndex = 5;
    OBL_CHECK_EQ(lighting::DayColor(*weather, 5.25f), 0x3f3f3fu); // 255 * 0.25 = 63.75
    weather->m_lightKeys[6].m_color = 0x10c8ff;
    OBL_CHECK_EQ(lighting::DayColor(*weather, 5.5f), 0x08647fu); // (16, 200, 255) * 0.5 -> 8, 100, 127
    // across midnight: lower key 23, upper key 0, fraction = hour - 23
    weather->m_lightIndex = 0;
    weather->m_previousLightIndex = 23;
    weather->m_lightKeys[23].m_color = 0x000000;
    weather->m_lightKeys[0].m_color = 0x808080;
    OBL_CHECK_EQ(lighting::DayColor(*weather, 23.5f), 0x404040u);
}

OBL_TEST_CASE("weather_lighting.tint_blends_for_a_quarter_hour_and_wraps_when_subtracted")
{
    auto weather = NewWeather();
    weather->m_currentState = 3;
    weather->m_previousState = 1;
    weather->m_stateColors[3] = 0x40'80'c0;
    weather->m_stateColors[1] = 0x000000;
    OBL_CHECK_EQ(lighting::BlendedTint(*weather, 0.125), 0x204060u); // half way: 32, 64, 96
    OBL_CHECK_EQ(lighting::BlendedTint(*weather, 0.0), 0u);
    OBL_CHECK_EQ(lighting::SubtractTint(0xab102030u, 0x201040u), 0xabf010f0u); // each byte wraps on its own, the top byte stays
}

OBL_TEST_CASE("weather_lighting.the_change_time_is_taken_a_day_back_when_it_lies_ahead")
{
    auto weather = NewWeather();
    weather->m_weatherTime = 22.0f;
    OBL_CHECK_EQ(lighting::HoursSinceChange(*weather, 22.5f), 0.5);
    OBL_CHECK_EQ(lighting::HoursSinceChange(*weather, 1.0f), 3.0); // the weather changed at 22:00 the day before
}

OBL_TEST_CASE("weather_lighting.a_strike_is_due_only_for_a_settled_armed_storm")
{
    auto weather = NewWeather();
    weather->m_currentState = weather->m_previousState = 5;
    weather->m_strikes = 1;
    weather->m_nextStrikeTime = 100.0f;
    OBL_CHECK(lighting::StrikeDue(*weather, 12.0f, 100.5f));
    OBL_CHECK(!lighting::StrikeDue(*weather, 12.0f, 100.0f)); // not yet: strictly after
    weather->m_strikes = 0;
    OBL_CHECK(!lighting::StrikeDue(*weather, 12.0f, 200.0f));
    weather->m_strikes = 1;
    weather->m_manualLighting = 1;
    OBL_CHECK(!lighting::StrikeDue(*weather, 12.0f, 200.0f));
    weather->m_manualLighting = 0;
    weather->m_previousState = 4; // still blending in
    weather->m_weatherTime = 11.9f;
    OBL_CHECK(!lighting::StrikeDue(*weather, 12.0f, 200.0f));
    weather->m_weatherTime = 11.5f; // the quarter hour is over: the previous weather becomes 5 in this very call
    OBL_CHECK(lighting::StrikeDue(*weather, 12.0f, 200.0f));
    weather->m_currentState = 3;
    OBL_CHECK(!lighting::StrikeDue(*weather, 12.0f, 200.0f));
    weather->m_currentState = 20; // out of the colour table
    OBL_CHECK(LightingUnsupported(*weather, 12.0f, 0.0f, false));
}

OBL_TEST_CASE("weather_lighting.table_zero_scales_the_colour_and_saturates")
{
    auto weather = NewWeather(); // (64, 64, 64) every hour, normal weather
    FakeEnv env;
    UpdateLighting(*weather, 12.5f, 100.0f, true, env);
    OBL_CHECK_EQ(weather->m_lastColor & 0xffffffu, 0x404040u);
    OBL_CHECK_EQ(RedStep(*weather, 0, 31, true), 7);
    OBL_CHECK_EQ(GreenStep(*weather, 0, 31), 7);
    OBL_CHECK_EQ(BlueStep(*weather, 0, 31, true), 7);
    OBL_CHECK_EQ(RedStep(*weather, 0, 0, true), 0);
    OBL_CHECK_EQ(env.draws + env.releases, 0);
    // table 17 saturates every step above 0 (multiplier 255); table 8 is the identity (255 * (float)(1/255) = 1.00000006: a step never loses one)
    OBL_CHECK_EQ(RedStep(*weather, 17, 1, true), 31);
    OBL_CHECK_EQ(RedStep(*weather, 17, 0, true), 0);
    OBL_CHECK_EQ(BlueStep(*weather, 8, 31, true), 31);
    OBL_CHECK_EQ(BlueStep(*weather, 8, 12, true), 12);
    OBL_CHECK_EQ(RedStep(*weather, 16, 31, true), 31); // x2 brighter: clamped
    // RGB565: five-bit red/blue, six-bit green with 64 steps
    UpdateLighting(*weather, 12.5f, 100.0f, false, env);
    OBL_CHECK_EQ(RedStep(*weather, 8, 31, false), 31);
    OBL_CHECK_EQ(GreenStep(*weather, 8, 40), 40);
    OBL_CHECK_EQ(GreenStep(*weather, 17, 63), 63);
    OBL_CHECK_EQ(BlueStep(*weather, 8, 31, false), 31);
}

OBL_TEST_CASE("weather_lighting.heat_wave_and_manual_colour_brighten")
{
    auto weather = NewWeather();
    FakeEnv env;
    weather->m_currentState = weather->m_previousState = 0;
    UpdateLighting(*weather, 12.5f, 100.0f, true, env); // x1.3: 31 * 0.32627 = 10.1
    OBL_CHECK_EQ(RedStep(*weather, 0, 31, true), 10);
    weather = NewWeather();
    weather->m_manualLighting = 1;
    weather->m_manualColor = 0x7f20ff10u;
    UpdateLighting(*weather, 12.5f, 100.0f, true, env); // the manual colour replaces the day and is multiplied by 3
    OBL_CHECK_EQ(weather->m_lastColor, 0x7f20ff10u); // top byte included
    OBL_CHECK_EQ(RedStep(*weather, 0, 10, true), 3); // red 0x20: 10 * (32 / 255) * 3 = 3.76
    OBL_CHECK_EQ(GreenStep(*weather, 0, 10), 30); // green 0xff: 255 * (float)(1/255) rounds to 1.0f, times 3, times 10
    OBL_CHECK_EQ(BlueStep(*weather, 0, 31, true), 5); // blue 0x10: 31 * (16 / 255) * 3 = 5.8
}

OBL_TEST_CASE("weather_lighting.flash_phases")
{
    FakeEnv env;
    auto base = NewWeather();
    UpdateLighting(*base, 12.5f, 100.0f, true, env);
    auto flash = [&](float since, bool lightmap) {
        auto weather = NewWeather();
        weather->m_lightFlash = 1;
        weather->m_flashTime = 100.0f - since;
        static char bitmap[4];
        weather->m_lightningLightmap = lightmap ? bitmap : nullptr;
        env = FakeEnv{};
        UpdateLighting(*weather, 12.5f, 100.0f, true, env);
        return weather;
    };
    auto first = flash(0.1f, false); // brightened: red and green x2, blue x4 (31 * 1.0039 clamps)
    OBL_CHECK_EQ(RedStep(*first, 0, 31, true), 15);
    OBL_CHECK_EQ(GreenStep(*first, 0, 31), 15);
    OBL_CHECK_EQ(BlueStep(*first, 0, 31, true), 31);
    OBL_CHECK_EQ(BlueStep(*first, 0, 30, true), 30);
    OBL_CHECK_EQ(first->m_lightFlash, 1);
    auto gap = flash(0.2f, false); // between the flashes: the plain colour
    OBL_CHECK_EQ(RedStep(*gap, 0, 31, true), RedStep(*base, 0, 31, true));
    OBL_CHECK_EQ(env.draws, 0);
    auto second = flash(0.4f, false);
    OBL_CHECK_EQ(RedStep(*second, 0, 31, true), 15);
    auto drawing = flash(0.5f, true); // the bolt is drawn, the flash goes on
    OBL_CHECK_EQ(env.draws, 1);
    OBL_CHECK_EQ(drawing->m_lightFlash, 1);
    OBL_CHECK(drawing->m_lightningLightmap != nullptr);
    auto ended = flash(0.7f, true); // over: the bitmap is released and the flag cleared
    OBL_CHECK_EQ(env.releases, 1);
    OBL_CHECK_EQ(env.draws, 0);
    OBL_CHECK_EQ(ended->m_lightFlash, 0);
    OBL_CHECK(ended->m_lightningLightmap == nullptr);
    auto endedEmpty = flash(0.7f, false);
    OBL_CHECK_EQ(env.releases, 0);
    OBL_CHECK_EQ(endedEmpty->m_lightFlash, 0);
}

OBL_TEST_CASE("weather_lighting.previous_weather_follows_the_current_one_after_the_blend")
{
    auto weather = NewWeather();
    FakeEnv env;
    weather->m_currentState = 3;
    weather->m_previousState = 1;
    weather->m_weatherTime = 12.0f;
    UpdateLighting(*weather, 12.1f, 100.0f, true, env);
    OBL_CHECK_EQ(weather->m_previousState, 1u); // 0.1 h: still blending
    UpdateLighting(*weather, 12.3f, 100.0f, true, env);
    OBL_CHECK_EQ(weather->m_previousState, 3u);
}
