#pragma once

#include "game/weather/weather_state.h"

#include "core/float_to_int.h"

#include <cstdint>
#include <cstring>

// The small Weather methods the sliders and the disaster scheduler call (docs/specs/weather.md): the colour override
// (weather.set-override-red-driver/Green/Blue, GetOverrideRed/Green/Blue), the lightning controls (SetThunderLevel, SetLightningArea,
// SetLightningTargetByName) and GetCurrentModifiers. The target search is written against an Env for the object manager (the game side
// implements it with the building list, the unit tests with vectors).
namespace obl::weather {

inline constexpr float kColorScale = 255.0f;
inline constexpr float kColorStep = 0.003921568859368563f; // (float)(1/255)
inline constexpr float kStrikeSpacing = 0.7f; // world-time units per level step between strikes
inline constexpr float kStrikeDelay = 3.0f;
inline constexpr int kMaxThunderLevel = 10; // the strike delay counts down from this
inline constexpr uint32_t kMaxTargetCandidates = 100;

inline void SetOverrideRed(weather::Weather& weather, float value)
{
    const uint32_t channel = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(value) * kColorScale)) & 0xff;
    weather.m_manualColor = (channel << 16) + (weather.m_manualColor & 0xffff);
}
inline void SetOverrideGreen(weather::Weather& weather, float value)
{
    const uint32_t channel = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(value) * kColorScale)) & 0xff;
    weather.m_manualColor = (channel << 8) + (weather.m_manualColor & 0xff00ff);
}
inline void SetOverrideBlue(weather::Weather& weather, float value)
{
    const uint32_t channel = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(value) * kColorScale)) & 0xff;
    weather.m_manualColor = channel + (weather.m_manualColor & 0xffff00);
}

inline double OverrideChannel(const weather::Weather& weather, int shift)
{
    return static_cast<double>(static_cast<int>((weather.m_manualColor >> shift) & 0xff)) * static_cast<double>(kColorStep);
}
inline double GetOverrideRed(const weather::Weather& weather)
{
    return OverrideChannel(weather, 16);
}
inline double GetOverrideGreen(const weather::Weather& weather)
{
    return OverrideChannel(weather, 8);
}
inline double GetOverrideBlue(const weather::Weather& weather)
{
    return OverrideChannel(weather, 0);
}

inline float NextStrikeTime(int level, float now)
{
    const double steps = static_cast<double>(static_cast<uint32_t>(kMaxThunderLevel - level));
    return static_cast<float>(steps * static_cast<double>(kStrikeSpacing) + static_cast<double>(now) + static_cast<double>(kStrikeDelay));
}

inline void SetThunderLevel(weather::Weather& weather, int level, float worldTime)
{
    if (weather.m_strikeLevel == 0) {
        weather.m_strikes = 1;
        weather.m_strikeLevel = level;
        weather.m_nextStrikeTime = NextStrikeTime(level, worldTime);
        return;
    }
    weather.m_strikes = 0;
    weather.m_strikeLevel = level;
}

// weather.set-lightning-area-driver(left, top, right, bottom): the rectangle a random strike point is drawn in (`Disaster Rect`).
inline void SetLightningArea(weather::Weather& weather, int left, int top, int right, int bottom)
{
    weather.m_strikeArea.left = left;
    weather.m_strikeArea.top = top;
    weather.m_strikeArea.right = right;
    weather.m_strikeArea.bottom = bottom;
}

template<class Env> void SetLightningTargetByName(weather::Weather& weather, const char* key, Env& env)
{
    int32_t candidates[kMaxTargetCandidates];
    uint32_t matches = 0;
    for (uint32_t i = env.BuildingCount(); i-- > 0;) {
        if (!env.KeyEquals(i, key)) continue;
        if (matches < kMaxTargetCandidates) candidates[matches] = env.BuildingId(i);
        ++matches;
    }
    if (matches == 0) return;
    if (matches > kMaxTargetCandidates) matches = kMaxTargetCandidates;
    weather.m_strikes = 1;
    weather.m_hasStrikeTarget = 1;
    weather.m_strikeBuildingId = candidates[static_cast<uint32_t>(env.Rand()) % matches];
}

// weather.get-current-modifiers-driver: the 26 desire modifiers of the active weather.
inline float* CurrentModifiers(weather::Weather& weather)
{
    return weather.m_desireModifiers;
}

} // namespace obl::weather
