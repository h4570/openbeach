#pragma once

#include "game/weather/weather_state.h"
#include "game/weather/weather_parameters.h"

#include <cstddef>
#include <cstdint>

// Native lifecycle: SetGlobal, RandomizeRainDrops and UpdateLighting are supplied
// by the facade. Destruction frees the runtime bitmap through Env::Delete and
// clears the global alias; the native schedule releases its own entries.
namespace obl::weather {

namespace lifecycle {

inline constexpr uint32_t kLastLightKey = 23; // `m_previousLightIndex` starts on the last key, `m_lightIndex` on the first

inline weather::WeatherEntry* ScheduleElement(const std::deque<std::unique_ptr<WeatherEntry>>& schedule, uint32_t index)
{
    return schedule[index].get();
}

} // namespace lifecycle

// Preserve startup order: global alias, palette and state, rain RNG draws, then
// the first lighting pass. The native class initializes all runtime scratch fields.
template<class Env> weather::Weather* ConstructWeather(weather::Weather& weather, Env& env, const Parameters& parameters)
{
    env.SetGlobal(&weather);
    weather.m_schedule.clear();
    for (uint32_t i = 0; i < 24; ++i) weather.m_lightKeys[i] = parameters.lightKeys[i];
    weather.m_stateColors[0] = parameters.stateColors[0];
    weather.m_stateColors[1] = parameters.stateColors[1];
    weather.m_currentState = 1;
    weather.m_previousState = 1;
    for (uint32_t i = 2; i < 14; ++i) weather.m_stateColors[i] = parameters.stateColors[i];
    weather.m_lightIndex = 0;
    weather.m_previousLightIndex = lifecycle::kLastLightKey;
    weather.m_lastColor = 0;
    for (float& modifier : weather.m_desireModifiers) modifier = 0.0f;
    env.RandomizeRainDrops();
    weather.m_eventCount = 0;
    weather.m_nextEvent = 0;
    weather.m_lightFlash = 0;
    weather.m_manualLighting = 0;
    weather.m_flashColor = 0;
    weather.m_manualColor = 0;
    weather.m_rainChannel = -1;
    weather.m_rainKind = 0;
    weather.m_lightningLightmap = nullptr;
    weather.m_hasStrikeTarget = 0;
    weather.m_strikes = 0;
    weather.m_strikeBuildingId = -1;
    weather.m_nextStrikeTime = 0.0f;
    weather.m_strikeLevel = 0;
    weather.m_dayOffset = 0;
    weather.m_currentDayOffset = 0;
    // These scratch fields are normalized out of the hash until gameplay writes them.
    weather.m_weatherTime = 0.0f;
    weather.m_flashTime = 0.0f;
    weather.m_strikeArea = {};
    for (float& coordinate : weather.m_strikePoint) coordinate = 0.0f;
    env.UpdateLighting(weather);
    return &weather;
}

template<class Env> void DestructWeather(weather::Weather& weather, Env& env)
{
    weather.m_schedule.clear();
    if (weather.m_lightningLightmap) {
        env.Delete(weather.m_lightningLightmap);
        weather.m_lightningLightmap = nullptr;
    }
    env.ResetShared();
}

} // namespace obl::weather
