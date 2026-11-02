#pragma once

#include "core/float_to_int.h"
#include "game/messages/message_state.h"
#include "game/objects/object_staff.h"
#include "script_types.h"
#include "game/objects/script_program_exec.h"

#include <cstdint>

// The script variables of the staff base class: `GetValue` and `SetValue`
// (staff and shark script variables, docs/architecture/objects.md). One `switch` over the variable number of the script
// (`Get`/`Set`/`If`, the numbers are the script API of script_program_api.h); a number the base class does not know goes to the human base class's `GetValue` /
// `SetValue`, so the movement variables and the literals stay where they are. The six professions answer their own numbers
// first and hand the rest to these two (a later step of the same item).
// Pure logic over object::Staff: everything that reaches the game (objects by id, the clock, the building's serve point) goes through an
// `Env` that the game side maps onto the game's services (src/game/objects/staff_values.cpp) and the unit test onto a fake.
//
// GetEnv:
//   object::Base* Object(int32_t id)                         the object of an id (null for an unknown id)
//   bool IsOpen(object::Base*)                               the building's IsOpen
//   void ServePosition(object::Base*, float& x, float& y)    the building's serve position, the first two floats
//   float GameDayTime()                                            current hour within the simulation day
//   ScriptValue Base(Staff&, int32_t key, intptr_t data)      the human base class's GetValue
// SetEnv:
//   int32_t FloatToIntWrap(int32_t floatBits)                                script-to-integer conversion of a float is the game's own truncation
//   void Base(Staff&, int32_t key, intptr_t data, int32_t type, int32_t bits)   the human base class's SetValue
namespace obl::staff {

using object::Staff;
using script::ScriptValue;
namespace ex = script_program::exec;

constexpr int32_t kByte = ex::kTypeByte, kInt = ex::kTypeInt;

// The numbers each dispatcher answers itself; every other number (negative ones too: the compare is unsigned) is the base class's. The
// unit test checks that the switches below agree with it.
inline bool HandlesGet(int32_t key)
{
    switch (key) {
    case 0x00:
    case 0x05:
    case 0x2b:
    case 0x5d:
    case 0x74:
    case 0x75:
    case 0x7b:
    case 0x7c:
    case 0x7d:
    case 0x7e:
    case 0x7f:
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
    case 0xac: return true;
    default: return false;
    }
}
inline bool HandlesSet(int32_t key)
{
    return key == 0x47 || key == 0x80 || key == 0x84 || key == 0x105;
}

// `AtWork` (0x7d): the home building exists, the hour of the day lies in the shift and the building is open. The hour is the truncated
// float read as unsigned; a shift that ends before it starts runs through midnight (the window is [start, end) either way).
template<class Env> bool AtWork(const Staff& s, Env& env)
{
    object::Base* home = env.Object(s.m_HomeID);
    if (!home) return false;
    const uint32_t hour = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(env.GameDayTime())));
    const uint32_t start = static_cast<uint32_t>(s.m_StartTime), end = static_cast<uint32_t>(s.m_EndTime);
    const bool onShift = start > end ? (hour >= start || hour < end) : (hour >= start && hour < end);
    return onShift && env.IsOpen(home);
}

// `AtHome` (0xac): the serve point of the home building is within the integer part of 0.1 of the member's own position on both axes,
// i.e. the truncated differences are 0 (the absolute value of the most negative int stays negative, so it counts as near). The x test comes
// first and the y difference is only computed after it passed.
template<class Env> bool AtHome(const Staff& s, Env& env)
{
    float x = 0, y = 0;
    env.ServePosition(env.Object(s.m_HomeID), x, y);
    const auto Near = [](int32_t difference) {
        const uint32_t sign = static_cast<uint32_t>(difference >> 31);
        const int32_t magnitude = static_cast<int32_t>((static_cast<uint32_t>(difference) ^ sign) - sign);
        return static_cast<double>(magnitude) < static_cast<double>(0.1f);
    };
    if (!Near(FloatToIntWrap(static_cast<double>(x) - static_cast<double>(s.m_WorldXYZ[0])))) return false;
    return Near(FloatToIntWrap(static_cast<double>(y) - static_cast<double>(s.m_WorldXYZ[1])));
}

template<class Env> ScriptValue GetValue(Staff& s, int32_t key, intptr_t data, Env& env)
{
    const auto Int = [](int32_t v) { return ScriptValue{kInt, v}; };
    const auto Byte = [](uint8_t v) { return ScriptValue{kByte, v}; }; // the field's byte as it is: only the low byte of `bits` is defined
    const auto truncated = [&](float v) { return Int(FloatToIntWrap(static_cast<double>(v))); };
    const auto* message = static_cast<const messages::Message*>(s.m_currentMessage);

    switch (key) {
    case 0x00: return Int(message->sender);
    case 0x05: return Int(message->data);
    case 0x2b: return Int(message->flags);
    case 0x5d: return Int(message->extra);
    case 0x74: return truncated(s.m_WanderPos[0]);
    case 0x75: return truncated(s.m_WanderPos[1]);
    case 0x7b: return Int(s.m_StartTime);
    case 0x7c: return Int(s.m_EndTime);
    case 0x7d: return Int(AtWork(s, env) ? 1 : 0);
    case 0x7e: return Byte(s.m_DirectStaff);
    case 0x7f: return Int(s.m_GotoObject);
    case 0x80: return Byte(s.m_foundDirecting);
    case 0x81: return truncated(s.m_DirectPos[0]);
    case 0x82: return truncated(s.m_DirectPos[1]);
    case 0x83: return Int(s.m_IdleTimePerHour);
    case 0x84: return Int(s.m_LastAction);
    case 0xac: return Byte(AtHome(s, env));
    default: return env.Base(s, key, data);
    }
}

template<class Env> void SetValue(Staff& s, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    const ScriptValue value{type, bits};
    switch (key) {
    case 0x47: s.m_maxVel = static_cast<float>(ex::ToFloatWide(value)); return; // WalkSpeed (Mover's field; the guest also sets its force)
    case 0x80: // FoundDirecting: the bits are read as a float whatever the type says; NaN and anything up to 0.9 is false
        s.m_foundDirecting = static_cast<double>(ex::BitsToFloat(bits)) > 0.9 ? 1 : 0;
        return;
    case 0x84: s.m_LastAction = ex::ToInt(env, value); return;
    case 0x105: s.m_DebugInfo = ex::ToInt(env, value); return;
    default: env.Base(s, key, data, type, bits); return;
    }
}

} // namespace obl::staff
