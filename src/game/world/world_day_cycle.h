#pragma once

#include "game/messages/message_state.h"
#include "game/objects/staff_classes.h"
#include "game/world/world_state.h"

#include <cstdint>

// world::UpdateDayCycle: the day change (NEW_DAY to buildings, personnel and guests, ferry cursor, "today" guest counters,
// daily statistics), the hour change (NEW_HOUR to personnel 7-12) and the music day/night switch at 19:00 and 7:00.
// The function compares the hour the previous frame ended on with the current one,
// then stores the current hour for the next frame.
//
// The sending, the ferry manager, the music player and world::RollDailyStats stay behind an `Env` (the game side calls the game's
// functions), so the order of events is unit-tested on its own:
//   void PostToBuildings(Message&)   one Post per building, receiver = its id (message handler of the World)
//   void PostToClass(int cls, Message&)   world::PostMessageToClass: one Post per object of the class
//   void PostToStaff(Message&)       one Post per object of object_manager::GetNonServiceStaffList (personnel types 7-12)
//   void ResetFerryCursor()  void RollDailyStats()  void SetNight(bool)
namespace obl::day_cycle {

// Float to int by truncation toward zero (truncating): NaN and values outside int give the "integer indefinite" 0x80000000.
inline int Truncate(float value)
{
    return (value >= -2147483648.0f && value < 2147483648.0f) ? static_cast<int>(value) : INT32_MIN;
}

// "Below or unordered": a NaN hour counts as below.
inline bool Below(float value, float limit)
{
    return !(value >= limit);
}

constexpr float kNightStart = 19.0f;
constexpr float kDayStart = 7.0f;
constexpr int kBuildingClass = 2;

// The message's `extra`/`floatData` words carry no meaning here and are zero (`time` is set by Post).
inline messages::Message MakeMessage(int type, int day)
{
    messages::Message message{};
    message.sender = -2;
    message.receiver = -1;
    message.type = type;
    message.flags = 0;
    message.data = day;
    return message;
}

template<class Env> void UpdateDayCycle(world::World& world, Env& env)
{
    const float previous = world.m_lastGameDayTime;
    const float current = world.m_gameDayTime;

    if (Truncate(previous) > Truncate(current)) {
        messages::Message message = MakeMessage(messages::MT_NEW_DAY, world.m_gameDay);
        env.PostToBuildings(message);
        for (int cls : object::kHumanClasses) env.PostToClass(cls, message); // personnel, then guests
        env.ResetFerryCursor();
        for (int& value : world.m_todayGuestArrive) value = 0;
        for (int& value : world.m_todayGuestDepart) value = 0;
        for (int& value : world.m_todayGuestUnhappyDepart) value = 0;
        env.RollDailyStats();
    }

    if (Truncate(previous) != Truncate(current)) {
        messages::Message message = MakeMessage(messages::MT_NEW_HOUR, world.m_gameDay);
        env.PostToStaff(message);
    }

    // The 19:00 test excludes the 7:00 one; both read the hour of the previous frame, which is still stored at that point.
    if (Below(previous, kNightStart) && !Below(current, kNightStart))
        env.SetNight(true);
    else if (Below(previous, kDayStart) && !Below(current, kDayStart))
        env.SetNight(false);
    world.m_lastGameDayTime = current;
}

} // namespace obl::day_cycle
