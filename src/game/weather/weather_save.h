#pragma once

#include "game/weather/weather_lifecycle.h"

namespace obl::weather {

// One record table, in binary save order. Area preserves the unsigned stream type;
// Bitmap serializes presence and recreates the resource at this point on load.
// Dword is the cached last tint, kept by saves and omitted by snapshots.
template<class State, class Visitor> void VisitFields(State& weather, Visitor& visitor)
{
    visitor.UInt("light_index", weather.m_lightIndex);
    visitor.UInt("light_previous_index", weather.m_previousLightIndex);
    visitor.Float("time", weather.m_weatherTime);
    visitor.Floats("desire_modifiers", weather.m_desireModifiers, 26);
    visitor.Int("next_event", weather.m_nextEvent);
    visitor.Int("event_count", weather.m_eventCount);
    visitor.Bool("light_flash", weather.m_lightFlash);
    visitor.Int("light_colour", weather.m_flashColor);
    visitor.Float("flash_time", weather.m_flashTime);
    visitor.Bool("manual", weather.m_manualLighting);
    visitor.UInt("manual_colour", weather.m_manualColor);
    visitor.Float("strike_time", weather.m_nextStrikeTime);
    visitor.Int("strike_level", weather.m_strikeLevel);
    visitor.Int("strike_building", weather.m_strikeBuildingId);
    visitor.Bool("striking_building", weather.m_hasStrikeTarget);
    visitor.Area("strike_area.top", weather.m_strikeArea.top);
    visitor.Area("strike_area.bottom", weather.m_strikeArea.bottom);
    visitor.Area("strike_area.left", weather.m_strikeArea.left);
    visitor.Area("strike_area.right", weather.m_strikeArea.right);
    visitor.Float("strike_point.x", weather.m_strikePoint[0]);
    visitor.Float("strike_point.y", weather.m_strikePoint[1]);
    visitor.Float("strike_point.z", weather.m_strikePoint[2]);
    visitor.Bool("strikes", weather.m_strikes);
    visitor.Bitmap("lightning_lightmap", weather);
    visitor.UInt("previous_state", weather.m_previousState);
    visitor.UInt("state", weather.m_currentState);
    visitor.Dword("last_colour", weather.m_lastColor);
    visitor.Int("day_offset", weather.m_dayOffset);
    visitor.Int("current_day_offset", weather.m_currentDayOffset);
}

// Schedule entries come from the scenario, not the save. Loading shifts their days
// by the stored cumulative offset; tone tables and rain audio keep their runtime state.
// Used by the obl.save reader (game/world/save_table_reader.h).
template<class Visitor> void LoadFields(Weather& weather, Visitor& visitor)
{
    VisitFields(weather, visitor);
    for (uint32_t i = 0; i < static_cast<uint32_t>(weather.m_eventCount); ++i) {
        auto& entry = *weather.m_schedule[i];
        entry.m_day = static_cast<int>(static_cast<uint32_t>(entry.m_day) + static_cast<uint32_t>(weather.m_currentDayOffset));
    }
}

} // namespace obl::weather
