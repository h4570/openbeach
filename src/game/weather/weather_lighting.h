#pragma once

#include "game/weather/weather_state.h"

#include "core/float_to_int.h"
#include "game/weather/weather_control.h"
#include "game/weather/weather_strike.h"

#include <cstdint>

namespace obl::weather {

namespace lighting {

inline constexpr uint32_t kLightKeys = 24;
inline constexpr uint32_t kStates = 14; // entries of Weather::m_stateColors
inline constexpr float kDayHours = 24.0f;
inline constexpr float kNoon = 12.0f; // the wrap-around key (index 0) only counts after noon
inline constexpr float kBlendHours = 0.25f; // the new weather has taken over
inline constexpr float kBlendScale = 4.0f; // fraction = hours since the change * 4
inline constexpr float kFlashFirstEnd = 0.15f;
inline constexpr float kFlashGapEnd = 0.3f;
inline constexpr float kFlashSecondEnd = 0.45f;
inline constexpr float kFlashEnd = 0.6f;
inline constexpr float kHeatWave = 1.3f; // factor for m_currentState 0
inline constexpr float kManual = 3.0f;
inline constexpr float kBlueFlash = 4.0f; // blue quadruples during the flash, red and green double
inline constexpr float kLimit555 = 31.0f;
inline constexpr float kLimit565Green = 63.0f;
inline constexpr uint32_t kTableCount = 18;
inline constexpr uint32_t kTableEntries = 0x80; // 0x100 B

// Hours since the weather became current: the hour minus the change time, with the change time taken a day back when it lies ahead of the hour
// (the schedule is cyclic).
inline double HoursSinceChange(const weather::Weather& weather, float hour)
{
    const double change = static_cast<double>(weather.m_weatherTime);
    return hour >= weather.m_weatherTime ? static_cast<double>(hour) - change
                                         : static_cast<double>(hour) - (change - static_cast<double>(kDayHours));
}

inline bool StateValid(const weather::Weather& weather)
{
    return weather.m_lightIndex < kLightKeys && weather.m_previousLightIndex < kLightKeys && weather.m_currentState < kStates &&
            weather.m_previousState < kStates;
}

inline bool StrikeDue(const weather::Weather& weather, float hour, float gameTime)
{
    if (weather.m_manualLighting || weather.m_currentState != 5) return false;
    if (weather.m_previousState != 5 && !(HoursSinceChange(weather, hour) > static_cast<double>(kBlendHours))) return false;
    return weather.m_strikes != 0 && gameTime > weather.m_nextStrikeTime;
}

// Moves the key pair forward while the hour has passed the upper key; key 0 (the wrap to the next day) only after the hour came below noon.
inline void AdvanceLightIndex(weather::Weather& weather, float hour)
{
    for (;;) {
        const uint32_t index = weather.m_lightIndex;
        if (!(hour >= weather.m_lightKeys[index].m_hour)) break;
        if (index == 0 && !(hour < kNoon)) break;
        weather.m_lightIndex = (index + 1) % kLightKeys;
        weather.m_previousLightIndex = (weather.m_previousLightIndex + 1) % kLightKeys;
    }
}

inline uint32_t Byte(uint32_t color, int shift)
{
    return (color >> shift) & 0xff;
}

inline uint32_t DayColor(const weather::Weather& weather, float hour)
{
    const weather::LightKey& upper = weather.m_lightKeys[weather.m_lightIndex];
    const weather::LightKey& lower = weather.m_lightKeys[weather.m_previousLightIndex];
    double base, span;
    if (weather.m_lightIndex == 0) { // the pair wraps around midnight: the lower key is the last of the day
        base = hour >= lower.m_hour ? static_cast<double>(lower.m_hour)
                                    : static_cast<double>(lower.m_hour) - static_cast<double>(kDayHours);
        span = (static_cast<double>(kDayHours) - static_cast<double>(lower.m_hour)) + static_cast<double>(upper.m_hour);
    } else {
        base = lower.m_hour;
        span = static_cast<double>(upper.m_hour) - static_cast<double>(lower.m_hour);
    }
    const double fraction = static_cast<double>(hour) - base / span;
    const double lowerWeight = 1.0 - fraction;
    uint32_t color = 0;
    for (int shift : {16, 8, 0}) {
        const double value =
                static_cast<double>(Byte(lower.m_color, shift)) * lowerWeight + static_cast<double>(Byte(upper.m_color, shift)) * fraction;
        color |= (static_cast<uint32_t>(FloatToIntWrap(value)) & 0xff) << shift;
    }
    return color;
}

// The tint during a weather change: the previous tint fades into the current one over 0.25 hours, per channel and truncated.
inline uint32_t BlendedTint(const weather::Weather& weather, double hoursSinceChange)
{
    const double fraction = hoursSinceChange * static_cast<double>(kBlendScale);
    const double previousWeight = 1.0 - fraction;
    const uint32_t current = weather.m_stateColors[weather.m_currentState];
    const uint32_t previous = weather.m_stateColors[weather.m_previousState];
    uint32_t color = 0;
    for (int shift : {0, 16, 8}) {
        const double value =
                static_cast<double>(Byte(current, shift)) * fraction + static_cast<double>(Byte(previous, shift)) * previousWeight;
        color |= (static_cast<uint32_t>(FloatToIntWrap(value)) & 0xff) << shift;
    }
    return color;
}

// day - tint per channel, each wrapping in its byte; the top byte stays.
inline uint32_t SubtractTint(uint32_t day, uint32_t tint)
{
    uint32_t color = day & 0xff000000u;
    for (int shift : {0, 8, 16}) color |= ((Byte(day, shift) - Byte(tint, shift)) & 0xff) << shift;
    return color;
}

// The multiplier of one channel for tone table `table` (1..17): tables 1..7 lean from the channel value `value` (0..255) toward white in eighths,
// 8 is white, 9..16 are white brightened by 1/8 .. 2x, 17 is the constant 255 (every nonzero step saturates).
inline double ToneMultiplier(uint32_t table, double value)
{
    constexpr double full = 255.0;
    const double step = static_cast<double>(kColorStep);
    switch (table) {
    case 1: return (full * 0.125 + value * 0.875) * step;
    case 2: return (full * 0.25 + value * 0.75) * step;
    case 3: return (full * 0.375 + value * 0.625) * step;
    case 4: return ((full + value) * 0.5) * step;
    case 5: return (full * 0.625 + value * 0.375) * step;
    case 6: return (full * 0.75 + value * 0.25) * step;
    case 7: return (full * 0.875 + value * 0.125) * step;
    case 8: return full * step;
    case 9: return full * step * 1.125;
    case 10: return full * step * 1.25;
    case 11: return full * step * 1.375;
    case 12: return full * step * 1.5;
    case 13: return full * step * static_cast<double>(1.675f); // the constant is a float: 1.6749999523
    case 14: return full * step * 1.75;
    case 15: return full * step * 1.875;
    case 16: return full * step + full * step;
    default: return full;
    }
}

inline void FillToneTable(uint16_t* table, double red, double green, double blue, bool rgb555)
{
    const float greenLimit = rgb555 ? kLimit555 : kLimit565Green;
    const int redShift = rgb555 ? 10 : 11;
    uint16_t* const reds = table;
    uint16_t* const greens = table + 0x20;
    uint16_t* const blues = table + (rgb555 ? 0x40 : 0x60);
    for (int i = 0; i < 32; ++i) {
        float r = static_cast<float>(static_cast<double>(i) * red);
        float g = static_cast<float>(static_cast<double>(i) * green);
        double b = static_cast<double>(i) * blue;
        if (r > kLimit555) r = kLimit555;
        if (g > greenLimit) g = greenLimit;
        if (b > static_cast<double>(kLimit555)) b = static_cast<double>(kLimit555);
        reds[i] = static_cast<uint16_t>(static_cast<uint32_t>(FloatToIntWrap(r)) << redShift);
        greens[i] = static_cast<uint16_t>(static_cast<uint32_t>(FloatToIntWrap(g)) << 5);
        blues[i] = static_cast<uint16_t>(FloatToIntWrap(b));
    }
    if (rgb555) return;
    for (int i = 32; i < 64; ++i) {
        double g = static_cast<double>(i) * green;
        if (g > static_cast<double>(kLimit565Green)) g = static_cast<double>(kLimit565Green);
        greens[i] = static_cast<uint16_t>(static_cast<uint32_t>(FloatToIntWrap(g)) << 5);
    }
}

} // namespace lighting

inline bool LightingUnsupported(const weather::Weather& weather, float hour, float gameTime, bool targetFound)
{
    if (!lighting::StateValid(weather)) return true;
    return lighting::StrikeDue(weather, hour, gameTime) && lighting::StrikeDividesByZero(weather, targetFound);
}

template<class Env> void UpdateLighting(weather::Weather& weather, float hour, float gameTime, bool rgb555, Env& env)
{
    using namespace lighting;
    uint32_t color;
    if (weather.m_manualLighting) {
        color = weather.m_manualColor;
    } else {
        AdvanceLightIndex(weather, hour);
        uint32_t state = weather.m_currentState;
        uint32_t tint;
        if (state == 1 && weather.m_previousState == 1) {
            tint = weather.m_stateColors[1];
        } else {
            if (HoursSinceChange(weather, hour) > static_cast<double>(kBlendHours)) weather.m_previousState = state;
            if (state != weather.m_previousState) {
                tint = BlendedTint(weather, HoursSinceChange(weather, hour));
            } else {
                if (state == 5 && gameTime > weather.m_nextStrikeTime && weather.m_strikes) {
                    Strike(weather, gameTime, env);
                    state = weather.m_currentState; // read again for the tint and the subtraction below
                }
                tint = weather.m_stateColors[state];
            }
        }
        color = DayColor(weather, hour);
        if (state != 0) color = SubtractTint(color, tint);
    }

    const float red = static_cast<float>(Byte(color, 16));
    const float green = static_cast<float>(Byte(color, 8));
    const float blue = static_cast<float>(Byte(color, 0));
    const double step = static_cast<double>(kColorStep);
    double redMul = static_cast<double>(red) * step;
    float greenMul = static_cast<float>(static_cast<double>(green) * step);
    double blueMul = static_cast<double>(blue) * step;

    if (weather.m_lightFlash) {
        const double since = static_cast<double>(gameTime) - static_cast<double>(weather.m_flashTime);
        const float sinceFloat = static_cast<float>(since); // the later compares reload the value stored as a float
        const auto brighten = [&] {
            redMul *= 2.0;
            greenMul *= 2.0f;
            blueMul *= static_cast<double>(kBlueFlash);
        };
        if (!(since >= static_cast<double>(kFlashFirstEnd))) {
            brighten();
        } else if (!(sinceFloat >= kFlashGapEnd)) {
            // between the two flashes: nothing changes
        } else if (!(sinceFloat >= kFlashSecondEnd)) {
            brighten();
        } else if (!(sinceFloat >= kFlashEnd)) {
            env.DrawLightning();
            redMul = static_cast<double>(static_cast<float>(redMul));
            blueMul = static_cast<double>(static_cast<float>(blueMul));
        } else {
            if (weather.m_lightningLightmap) {
                env.ReleaseLightmap(weather.m_lightningLightmap);
                weather.m_lightningLightmap = nullptr;
                redMul = static_cast<double>(static_cast<float>(redMul));
                blueMul = static_cast<double>(static_cast<float>(blueMul));
            }
            weather.m_lightFlash = 0;
        }
    }
    if (weather.m_currentState == 0) {
        redMul *= static_cast<double>(kHeatWave);
        greenMul = static_cast<float>(static_cast<double>(greenMul) * static_cast<double>(kHeatWave));
        blueMul *= static_cast<double>(kHeatWave);
    }
    if (weather.m_manualLighting) {
        redMul *= static_cast<double>(kManual);
        greenMul = static_cast<float>(static_cast<double>(greenMul) * static_cast<double>(kManual));
        blueMul *= static_cast<double>(kManual);
    }

    FillToneTable(weather.m_toneTables[0], redMul, static_cast<double>(greenMul), blueMul, rgb555);
    weather.m_lastColor = weather.m_manualLighting ? color : (color & 0x00ffffffu) | (weather.m_lastColor & 0xff000000u);
    for (uint32_t table = 1; table < kTableCount; ++table)
        FillToneTable(
                weather.m_toneTables[table], ToneMultiplier(table, red), ToneMultiplier(table, green), ToneMultiplier(table, blue), rgb555);
}

} // namespace obl::weather
