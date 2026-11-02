#pragma once

#include "core/float_to_int.h"
#include "game/messages/message_state.h"
#include "game/objects/object_staff.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// Staff ReceiveMessage (takes the message, answers a bool), the common half of
// every profession's ReceiveMessage. It handles two messages itself (the new day and the new hour of the world clock), then hands every message to the
// script of the member and, when the script has no procedure for it, to the movement code of the base class (the human movement message handler).
// Those two are calls into the game (the interpreter of `script-object`, the movement code of `sim-movement`); what is spelled out here is the member's own
// part and the bracket around the calls.
//
// The logic is a template over an environment, so it runs on the game and in the unit tests:
//   env.Object(id)              object lookup by id (null for an unknown id; asked before the clock is read)
//   env.IsOpen(building)        whether the building is open (asked for any object, even a null one)
//   env.GameDayTime()           fractional simulation hour
//   env.World()                 the world (the cash, the costs of today and the wages of today of the hourly pay)
//   env.Push(flags)             pushes a thought of the member
//   env.Recalculate()           recalculates the member's happiness target
//   env.RunOnReceipt(message)   RunOnReceipt of the member's script (message by value): true = the script has a procedure for the type
//   env.Movement(message)       the human movement message handler (member, &message)
// The arithmetic runs in double: each operation is one double operation, stored to a float once.
namespace obl::staff {

namespace receive_detail {

constexpr float kHourlyIdleSeconds = 60.0f; //: the idle minutes of a fully unhappy member
constexpr int32_t kOverworkedHours = 10; // more hours in a row than this make the member say so
constexpr uint32_t kOverworkedThought = 0x80000000;

// The copy of the message passes its floats through a float load/store, which turns a signalling NaN into a quiet one.
inline float Quieted(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0) bits |= 0x00400000u;
    std::memcpy(&value, &bits, sizeof(bits));
    return value;
}

// MT_NEW_DAY: the salary the member expects grows by the daily factor of its definition (the recommended wage is read unsigned as a 64-bit integer), and the
// target of the happiness follows from the new expectation.
template<class Env> void NewDay(object::Staff& staff, Env& env)
{
    const double recommended = static_cast<double>(static_cast<uint32_t>(staff.m_staffDefinition->m_RecommendedWage));
    const double grown = recommended * static_cast<double>(staff.m_salaryExpectationGrowth);
    staff.m_SalaryExpectation = static_cast<float>((grown - recommended) + static_cast<double>(staff.m_SalaryExpectation));
    env.Recalculate();
}

// The hour of the day in the shift window [start, end) (through midnight when the shift ends before it starts), compared unsigned.
inline bool OnShift(const object::Staff& staff, uint32_t hour)
{
    const uint32_t start = static_cast<uint32_t>(staff.m_StartTime), end = static_cast<uint32_t>(staff.m_EndTime);
    return start > end ? (hour >= start || hour < end) : (hour >= start && hour < end);
}

// The wage of the hour: the cash goes down by the wage read signed (signed), the costs of today and the wages of today up by it read unsigned
// (64-bit): the same sums as the severance of the sack.
template<class Env> void PayWage(const object::Staff& staff, Env& env)
{
    auto& world = env.World();
    auto& player = *world.m_player;
    player.m_cash = static_cast<float>(static_cast<double>(player.m_cash) - static_cast<double>(staff.m_Wage));
    const double wage = static_cast<double>(static_cast<uint32_t>(staff.m_Wage));
    world.m_costsToday = static_cast<float>(wage + static_cast<double>(world.m_costsToday));
    world.m_wagesCostToday = static_cast<float>(wage + static_cast<double>(world.m_wagesCostToday));
}

// MT_NEW_HOUR: an hour of work in a row when the member is on shift and its building is open (and an overworked thought and the wage when that makes
// more than ten), an hour of rest otherwise; then the target of the happiness and the idle minutes of the hour follow from the new count.
template<class Env> void NewHour(object::Staff& staff, Env& env)
{
    object::Base* home = env.Object(staff.m_HomeID);
    const uint32_t hour = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(env.GameDayTime())));
    if (OnShift(staff, hour) && env.IsOpen(home)) {
        staff.m_hoursWorkedInRow = staff.m_hoursWorkedInRow + 1;
        if (static_cast<int32_t>(staff.m_hoursWorkedInRow) > kOverworkedHours) env.Push(kOverworkedThought);
        PayWage(staff, env);
    } else {
        staff.m_hoursWorkedInRow = staff.m_hoursWorkedInRow - 1;
        if (static_cast<int32_t>(staff.m_hoursWorkedInRow) < 0) staff.m_hoursWorkedInRow = 0;
    }
    env.Recalculate();
    // The minutes are 60 - 60 * happiness (the product of two floats is exact in a double), truncated toward zero.
    const double idle =
            static_cast<double>(kHourlyIdleSeconds) - static_cast<double>(staff.m_happiness) * static_cast<double>(kHourlyIdleSeconds);
    staff.m_IdleTimePerHour = FloatToIntWrap(idle);
}

} // namespace receive_detail

// ReceiveMessage. While the script and the movement code run, `m_currentMessage` is a copy of the message
// and is put back afterwards. The script is `object::StaffScript`, and the message goes to it by value, to the movement code by the pointer the caller passed.
template<class Env> bool ReceiveMessage(object::Staff& staff, const messages::Message& message, Env& env)
{
    using namespace receive_detail;
    if (message.type == messages::MT_NEW_DAY)
        NewDay(staff, env);
    else if (message.type == messages::MT_NEW_HOUR)
        NewHour(staff, env);

    void* const saved = staff.m_currentMessage;
    messages::Message copy = message;
    copy.floatData = Quieted(copy.floatData);
    staff.m_currentMessage = &copy;
    const bool handled = env.RunOnReceipt(message) ? true : env.Movement(message);
    staff.m_currentMessage = saved;
    return handled;
}

} // namespace obl::staff
