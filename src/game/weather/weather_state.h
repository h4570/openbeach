#pragma once

#include <cstdint>
#include <deque>
#include <memory>

namespace obl::weather {

struct Rect {
    int left{}, top{}, right{}, bottom{};
};

struct WeatherEntry {
    int m_kind{}; // 0..5 (HeatWave, Normal, ... ThunderStorm), copied to `m_currentState` on activation
    float m_hour{};
    int m_day{};
    float m_desireModifiers[26]{}; // added to every guest's desires on activation, then kept in Weather::m_desireModifiers
};

// Key of the day-light curve built by the weather constructor (not saved): an hour of the day and its tint colour.
struct LightKey {
    float m_hour{};
    uint32_t m_color{};
};

// World-owned native state. Runtime lighting and audio fields are not saved.
struct Weather {
    uint16_t m_toneTables[18][0x80]{};
    LightKey m_lightKeys[24]{};
    uint32_t m_lightIndex{};
    uint32_t m_previousLightIndex{};
    float m_weatherTime{};
    float m_nextStrikeTime{};
    int m_strikeLevel{};
    int m_strikeBuildingId{};
    int m_dayOffset{};
    int m_currentDayOffset{};
    uint8_t m_hasStrikeTarget{};
    Rect m_strikeArea{};
    float m_strikePoint[3]{};
    float m_desireModifiers[26]{}; // the active weather's modifiers, copied from the entry by the weather schedule update
    void* m_lightningLightmap{};
    std::deque<std::unique_ptr<WeatherEntry>> m_schedule{};
    int m_nextEvent{}; // first entry not yet activated
    int m_eventCount{}; // == m_schedule.size()
    int m_rainChannel{};
    int m_rainKind{};
    uint8_t m_lightFlash{};
    uint8_t m_strikes{};
    int m_flashColor{};
    float m_flashTime{};
    uint8_t m_manualLighting{};
    uint32_t m_manualColor{};
    uint32_t m_previousState{};
    uint32_t m_currentState{};
    uint32_t m_stateColors[14]{}; // [m_currentState] is the tint weather.update-lighting-driver subtracts from the daylight (0x00RRGGBB)
    uint32_t m_lastColor{};
};

// Renderer-owned view-space rain positions: 8000 (x, y) pairs, the most
// The rain draw walks (4000 for heavy rain, doubled when the view is taller than 600 pixels).
constexpr uint32_t kRainDropCount = 8000;
struct RainDropBuffer {
    uint32_t m_xy[kRainDropCount][2]{};
};

} // namespace obl::weather
