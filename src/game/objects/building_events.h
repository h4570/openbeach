#pragma once
#include "core/ascii_case.h"

#include <cstring>

namespace obl::building_events {

// The compare uses the unrounded sum of two stored floats against the world hour.
template<class Slot> bool PastEnd(const Slot& slot, float hour)
{
    return hour > static_cast<double>(slot.m_start) + slot.m_duration;
}

template<class Slot> bool CanStart(const Slot& slot, float hour)
{
    return hour > slot.m_start && hour < static_cast<double>(slot.m_start) + slot.m_duration;
}

template<class Events> int ChooseSlot(const Events& events, float hour)
{
    if (events.m_eventActive) {
        for (int i = 0; i < 3; ++i)
            if (!events.m_slots[i].m_actors.empty() || !events.m_slots[i].m_guests.empty()) return i;
    }
    for (int i = 0; i < 3; ++i) {
        const auto& slot = events.m_slots[i];
        if (hour <= slot.m_start || hour < static_cast<double>(slot.m_start) + slot.m_duration) return i;
    }
    return 0;
}

struct Cast {
    int type;
    int count;
    bool randomVariant;
};

inline Cast GetCast(const char* key)
{
    if (ascii::EqualsNoCase(key, "WEIGHTLIFTER")) return {16, 3, false};
    if (ascii::EqualsNoCase(key, "SINGING")) return {15, 1, false};
    if (ascii::EqualsNoCase(key, "CATWALK")) return {17, 3, false};
    if (ascii::EqualsNoCase(key, "CABARET")) return {14, 1, true};
    if (ascii::EqualsNoCase(key, "WETTSHIRT")) return {18, 4, false};
    if (ascii::EqualsNoCase(key, "AEROBICS")) return {20, 1, true};
    return {0, 0, false};
}

inline float GuestDelay(int random, float delay)
{
    // The constants are floats, but the intermediate operations stay in double precision.
    return static_cast<float>((static_cast<double>(random) * (1.0f / 32767.0f) * 0.6f + delay) - 0.3f);
}

} // namespace obl::building_events
