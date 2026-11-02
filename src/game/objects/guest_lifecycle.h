#pragma once

#include "game/objects/guest_update_opinions.h"

#include <cstdint>

// The decisions of CheckBedTime and CheckLeaveResort without their side effects (messages, thoughts,
// draws), docs/architecture/objects.md. Both are called from SearchDesire and return whether the guest was sent somewhere.
//
// NaN is decided as follows: "below" holds for an unordered pair, "greater" does not, so the decisions below are written with the same two primitives.
namespace obl::guest_lifecycle {

using guest_opinions::Below;

// Does the guest go to bed (or, without a hotel, leave) at this hour? `wake`/`bed` are the sleep window, `hour` the world's day time,
// `day` its game day. The window may wrap over midnight (wake > bed); on day 0 the evening after `bed` still counts when the window is
// the 7-23 kind, but the small hours before `wake` do not (nobody was in bed the night before the first day).
inline bool BedTimeProceeds(float wake, float bed, float hour, int32_t day)
{
    // a window that does not wrap, the hour already past `bed` and before `wake`, on a later day.
    if (Below(bed, wake) && hour > bed && Below(hour, wake) && day != 0) return true;
    if (!(bed > wake)) return false;
    if (hour > bed) return true;
    if (!Below(hour, wake)) return false;
    return day != 0;
}

// The last night draws when to leave: wake + 1 + 2 * rand / 32767. The constant is the float 3.0518509e-5, every operation one
// double rounding, one float store (53-bit precision).
inline float TimeToLeave(int32_t draw, float wake)
{
    const double step = static_cast<double>(draw) * static_cast<double>(3.0518509e-5f);
    return static_cast<float>(((step + step) + static_cast<double>(wake)) + 1.0);
}

// Slot of g_GuestAttractionCounts a guest counts in (the destructor decrements it; the constructor increments the same one):
// the attractiveness band of the definition (m_Attractive: < 0.2 or NaN = 0, <= 0.7 = 1, otherwise 2; both limits are doubles)
// plus three per gender.
inline int32_t AttractionSlot(float attractive, int32_t gender)
{
    const double value = attractive;
    const int band = Below(value, 0.2) ? 0 : (value > 0.7 ? 2 : 1);
    return band + gender * 3;
}

enum class LeaveAction : uint8_t {
    Unhappy, // happiness <= 0.25: leave at once, `m_leavingEarly`, a THIS_IS_CRAP thought
    GoHome, // the souvenir flag is set: a GO_HOME thought and leave
    Souvenirs, // the stay is over: set the flag and raise the souvenir desire (no answer)
    Stay,
};

// `nights`, `timeToLeave` and `souvenirsDecided` are the guest's fields of the same names.
inline LeaveAction LeaveDecision(float happiness, int32_t nights, float timeToLeave, float hour, bool souvenirsDecided)
{
    if (!(happiness > 0.25f)) return LeaveAction::Unhappy;
    const bool due = nights == 0 && Below(timeToLeave, hour);
    if (souvenirsDecided) return LeaveAction::GoHome;
    return due ? LeaveAction::Souvenirs : LeaveAction::Stay;
}

} // namespace obl::guest_lifecycle
