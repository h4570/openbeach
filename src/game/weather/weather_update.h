#pragma once

#include "game/weather/weather_state.h"

#include <cstdint>

// The weather schedule update, the weather entry insertion and weather.randomize-rain-drops-driver, docs/specs/weather.md. The logic
// is written against an Env that stands for everything outside the Weather struct: the owned schedule entries, the
// guest list, message posting, the lighting step and the rain sound. The game side (src/game/weather/weather.cpp) implements it with the
// game's own functions, the unit tests with plain vectors.
namespace obl::weather {

inline bool EntryDue(const weather::WeatherEntry& entry, uint32_t day, float hour)
{
    const uint32_t entryDay = static_cast<uint32_t>(entry.m_day);
    if (entryDay < day) return true;
    if (entryDay != day) return false;
    return !(entry.m_hour >= hour);
}

inline constexpr int kBadWeatherMessage = 72;

inline constexpr float kRainVolume = 0.8f; // volume of the rain loops and later adjustments
inline constexpr const char* kRainLoopLight = "rain_loop"; // m_currentState 3
inline constexpr const char* kRainLoopHeavy = "rain_heavy"; // m_currentState 4 and 5
inline constexpr int kRainKindNone = 0, kRainKindLight = 2, kRainKindHeavy = 3;

template<class Env> void UpdateSchedule(weather::Weather& weather, uint32_t day, float hour, Env& env)
{
    // Entries whose time has come, in order. The index is re-read from the struct on every round because a delivered message may run scripts.
    while (static_cast<uint32_t>(weather.m_nextEvent) < static_cast<uint32_t>(weather.m_eventCount)) {
        const weather::WeatherEntry& entry = env.Entry(static_cast<uint32_t>(weather.m_nextEvent));
        if (!EntryDue(entry, day, hour)) break;
        weather.m_previousState = weather.m_currentState;
        weather.m_currentState = static_cast<uint32_t>(entry.m_kind);
        weather.m_weatherTime = entry.m_hour;
        for (int i = 0; i < 26; ++i) weather.m_desireModifiers[i] = entry.m_desireModifiers[i];
        ++weather.m_nextEvent;

        typename Env::Message message{};
        message.sender = -2;
        message.type = kBadWeatherMessage;
        const uint32_t guests = env.GuestCount();
        for (uint32_t i = 0; i < guests; ++i) {
            env.ApplyModifiers(i);
            if (static_cast<int32_t>(weather.m_currentState) > 2) { // light rain and worse
                message.receiver = env.GuestId(i);
                env.Post(message);
            }
        }
    }
    env.UpdateLighting();

    // The schedule ran out: with a day offset it starts over, every entry moved by that many days.
    if (weather.m_nextEvent == weather.m_eventCount && weather.m_dayOffset != 0) {
        weather.m_currentDayOffset =
                static_cast<int>(static_cast<uint32_t>(weather.m_currentDayOffset) + static_cast<uint32_t>(weather.m_dayOffset));
        weather.m_nextEvent = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(weather.m_eventCount); ++i) {
            weather::WeatherEntry& entry = env.Entry(i);
            entry.m_day = static_cast<int>(static_cast<uint32_t>(entry.m_day) + static_cast<uint32_t>(weather.m_dayOffset));
        }
    }

    const int32_t kind = static_cast<int32_t>(weather.m_currentState);
    int wanted = kRainKindNone;
    const char* name = nullptr;
    if (kind == 3) {
        wanted = kRainKindLight;
        name = kRainLoopLight;
    } else if (kind > 3 && kind <= 5) {
        wanted = kRainKindHeavy;
        name = kRainLoopHeavy;
    }
    if (wanted == kRainKindNone) {
        if (weather.m_rainKind == 0) return;
        if (weather.m_rainChannel != -1) env.FadeOutRain(weather.m_rainChannel);
        weather.m_rainKind = 0;
        return;
    }
    if (weather.m_rainKind == wanted) {
        env.SetRainVolume(weather.m_rainChannel);
        return;
    }
    if (weather.m_rainChannel != -1) env.FadeOutRain(weather.m_rainChannel);
    const int channel = env.PlayRainLoop(name);
    weather.m_rainKind = wanted;
    weather.m_rainChannel = channel;
}

inline bool SortsBefore(const weather::WeatherEntry& element, const weather::WeatherEntry& entry)
{
    const uint32_t elementDay = static_cast<uint32_t>(element.m_day);
    const uint32_t entryDay = static_cast<uint32_t>(entry.m_day);
    if (elementDay != entryDay) return elementDay < entryDay;
    return !(element.m_hour >= entry.m_hour);
}

// Index at which the new entry goes: before the first element that does not sort before it, or the end. Equal times go in front of the old ones.
template<class Env> uint32_t InsertPosition(const weather::WeatherEntry& entry, uint32_t count, Env& env)
{
    for (uint32_t i = 0; i < count; ++i)
        if (!SortsBefore(env.Entry(i), entry)) return i;
    return count;
}

template<class Env> void InsertEntry(weather::Weather& weather, const weather::WeatherEntry& entry, Env& env)
{
    weather::WeatherEntry* stored = env.NewEntry(entry);
    const uint32_t position = InsertPosition(entry, env.Count(), env);
    env.InsertAt(position, stored);
    ++weather.m_eventCount;
}

template<class Rng> void RandomizeRainDrops(weather::RainDropBuffer& drops, int32_t viewWidth, int32_t viewHeight, Rng&& rng)
{
    if (viewWidth == 0 || viewHeight == 0) return;
    const uint32_t width = static_cast<uint32_t>(viewWidth);
    const uint32_t height = static_cast<uint32_t>(viewHeight);
    for (uint32_t i = 0; i < weather::kRainDropCount; ++i) {
        const uint32_t y = static_cast<uint32_t>(rng()) % height;
        const uint32_t x = static_cast<uint32_t>(rng()) % width;
        drops.m_xy[i][0] = x;
        drops.m_xy[i][1] = y;
    }
}

} // namespace obl::weather
