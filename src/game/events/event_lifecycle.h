#pragma once

#include "event_logic.h"

#include <cstdint>

namespace obl::disaster {

// The records in save order. A visitor provides:
//   Int(name, field)                  one record of a 4-byte signed field
//   Active(countName, idName, list)   a "uint" count record, then one `idName` record per running event (schedule index)
template<class M, class Visitor> void VisitFields(M& m, Visitor& v)
{
    v.Int("next_event", m.nextEvent);
    v.Int("event_count", m.numEvents);
    v.Int("current_event_count", m.numCurEvents);
    v.Active("current_events", "current_events[]", m.active);
    v.Int("day_offset", m.dayOffset);
    v.Int("current_day_offset", m.curDayOffset);
}

// Construct and destruct bind the manager to the world (the global the script reads); the counters start at zero.
template<class Env> void Construct(Env& env)
{
    env.SetGlobal(true);
    auto& m = env.State();
    m.nextEvent = m.numEvents = m.numCurEvents = 0;
    m.dayOffset = m.curDayOffset = 0;
}

template<class Env> void Destruct(Env& env)
{
    env.SetGlobal(false);
}

template<class Env, class Visitor> void LoadFields(Env& env, Visitor& visitor)
{
    auto& m = env.State();
    m.active.clear();
    VisitFields(m, visitor);
    for (int id : m.active) {
        const auto& event = env.Event(id);
        if (event.type == 2) env.Available(event.beerType, false);
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(m.numEvents); ++i) env.Event(i).day += static_cast<uint32_t>(m.curDayOffset);
}

} // namespace obl::disaster
