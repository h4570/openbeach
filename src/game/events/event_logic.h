#pragma once

#include "event_state.h"

namespace obl::disaster {

template<class Env> void AddEvent(Env& env, const Event& event)
{
    auto& m = env.State();
    uint32_t index = 0;
    while (index < static_cast<uint32_t>(m.numEvents)) {
        const auto& existing = env.Event(index);
        if (event.day < existing.day || (event.day == existing.day && event.time < existing.time)) break;
        ++index;
    }
    if (index < static_cast<uint32_t>(m.numEvents)) {
        env.InvalidInsertion();
        return;
    }
    env.Insert(index, event);
    ++m.numEvents;
}

template<class Env> void Start(Env& env, const Event& event, int index)
{
    switch (event.type) {
    case 0:
        for (int i = 0; i < event.scale; ++i) {
            int x = env.Rand() % (event.rect[2] - event.rect[0]) + event.rect[0];
            int y = env.Rand() % (event.rect[3] - event.rect[1]) + event.rect[1];
            env.Shark(index, event.rect, static_cast<float>(x), static_cast<float>(y));
        }
        break;
    case 1: env.Water(event.disasterValue); break;
    case 2:
        for (size_t i = 0; i < env.BuildingCount(); ++i) {
            if (env.BuildingBeer(i) == event.beerType) {
                env.Beer(i, event.replacementBeer);
                env.Strength(i, event.replacementStrength);
            }
        }
        env.Available(event.beerType, false);
        env.BeerMessage(event.beerType, false);
        break;
    case 3:
        env.Thunder(event.scale);
        env.Area(event.rect);
        break;
    case 4: env.Target(event.target ? event.target->c_str() : nullptr); break;
    }
}

template<class Env> void Finish(Env& env, const Event& event, int index)
{
    switch (event.type) {
    case 0:
        for (size_t i = 0; i < env.SharkCount(); ++i) env.Finished(i, index);
        break;
    case 1: env.Water(0); break;
    case 2:
        env.Available(event.beerType, true);
        env.BeerMessage(event.beerType, true);
        break;
    case 3: env.Thunder(0); break;
    }
}

template<class Env> void Update(Env& env)
{
    auto& m = env.State();
    uint32_t day = env.Day();
    float hour = env.Hour();
    for (uint32_t i = 0; i < static_cast<uint32_t>(m.numCurEvents); ++i) {
        int index = env.Active(i);
        const auto& event = env.Event(index);
        uint32_t endDay = event.day;
        double endHour = static_cast<double>(event.time) + event.duration;
        while (endHour >= 24) {
            int days = static_cast<int>(endHour / 24);
            endDay += days;
            endHour -= static_cast<double>(days) * 24;
        }
        if (endDay < day || (endDay == day && !(endHour >= hour))) {
            Finish(env, event, index);
            env.RemoveActive(i);
            if (i != static_cast<uint32_t>(m.numCurEvents - 1)) --i;
            --m.numCurEvents;
        }
    }
    while (static_cast<uint32_t>(m.nextEvent) < static_cast<uint32_t>(m.numEvents)) {
        const auto& event = env.Event(m.nextEvent);
        if (day < event.day || (day == event.day && !(hour > event.time))) break;
        Start(env, event, m.nextEvent);
        if (event.type != 4) {
            env.AddActive(m.nextEvent);
            ++m.numCurEvents;
        }
        ++m.nextEvent;
    }
    if (m.nextEvent == m.numEvents && m.dayOffset != 0 && m.numCurEvents == 0) {
        m.curDayOffset += m.dayOffset;
        m.nextEvent = 0;
        for (int i = 0; i < m.numEvents; ++i) env.Event(i).day += m.dayOffset;
    }
}

} // namespace obl::disaster
