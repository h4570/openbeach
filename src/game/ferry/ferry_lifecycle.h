#pragma once

#include "ferry_logic.h"

#include <cstdint>

namespace obl::ferry {

constexpr uint32_t kNoCursor = 0xffffffffu; // the saved cursor index of "past the end" (and of no cursor)

// One boat's records in save order. `B` is `Boat` or `const Boat`.
template<class B, class Visitor> void VisitBoat(B& boat, Visitor& v)
{
    v.Int("guest_info", boat.GuestInfo);
    v.Ints("guest_random", boat.GuestType, kGuestTypes);
    for (auto& text : boat.GuestKeyword) v.Keyword("guest_keywords[]", text);
    v.Ints("guest_counts", boat.GuestKeywordNumbers, kGuestTypes);
    v.Int("guests", boat.NumGuests);
    v.Float("time", boat.Time);
    v.Int("day", boat.Day);
    v.Int("time_flags", boat.TimeFlags);
}

// The records after the class name, in save order. `M` is `Manager` or `const Manager`. A visitor provides:
//   Boats(name, list)                 a "uint" count record, then VisitBoat for each boat
//   Cursor(name, m)                   the schedule index of the cursor ("uint", kNoCursor past the end or without one)
//   UInt/Int/Float(name, field)       one record of a 4-byte field
//   Floats/Ints(name, pointer, n)     a record of n values
//   Keyword(name, text)               one guest keyword of kKeywordSize bytes
template<class M, class Visitor> void VisitFields(M& m, Visitor& v)
{
    v.Boats("boats", m.schedule);
    v.Cursor("next_boat", m);
    v.Boats("performances", m.performance);
    v.Floats("departures.average_happiness", m.guests.fAvgHapp, kGuestTypes);
    v.UInt("departures.happy", m.guests.iHappyLeavers);
    v.UInt("departures.unhappy", m.guests.iUnhappyLeavers);
}

template<class M> void SetCursor(M& m, uint32_t current)
{
    if (current == kNoCursor) {
        m.cursor = m.schedule.end();
        return;
    }
    const auto steps = static_cast<int32_t>(current) < 0 ? 0u : current % (m.schedule.size() + 1);
    auto node = m.schedule.begin();
    for (uint32_t i = 0; i < steps; ++i) node = node == m.schedule.end() ? m.schedule.begin() : std::next(node);
    m.cursor = node;
}

// Replaces both queues and the fields. The cursor is read after the schedule (it indexes into it), so the load walks the fields in order.
// Used by the obl.save reader (game/world/save_table_reader.h).
template<class Visitor> void LoadFields(Manager& m, Visitor& visitor)
{
    m.schedule.clear();
    m.performance.clear();
    m.cursor.reset();
    VisitFields(m, visitor);
}

} // namespace obl::ferry
