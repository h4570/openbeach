#pragma once

#include "core/float_to_int.h"
#include "core/ascii_case.h"
#include "game/objects/guest_script_values.h" // SetSpriteDirection, TextBits: the same sprite steps as the guest's
#include "game/ui/sprite.h"
#include "script_types.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "game/objects/object_staff.h"
#include "game/objects/script_program_exec.h"

#include <cstdint>
#include <cstring>

// The script variables of four staff professions, the overrides of GetValue and SetValue in front of the staff GetValue / SetValue
// (src/game/objects/staff_script_values.h, staff script values, docs/architecture/objects.md):
//   CleanerGetValue / CleanerSetValue, MechanicGetValue / MechanicSetValue,
//   RepGetValue / RepSetValue, SecurityGetValue / SecuritySetValue.
// Each is one switch over the variable number; every other number is one call of the staff GetValue / SetValue.
// Cleaner, Mechanic and Security switch over the number as unsigned, the Rep's GetValue uses signed comparisons; either way a negative number is the base class's.
// Pure logic over the layouts; what reaches the game goes through an `Env` (game side: src/game/objects/staff_profession_values.cpp):
//
// GetEnv:
//   ScriptValue Base(Staff&, int32_t key, intptr_t data)      the staff GetValue
//   object::Base* Object(int32_t id)                         object by id (a null result is not checked: ids from scripts are valid)
//   bool IsOpen(object::Base*)                               whether the building is open
//   float GameDayTime()                                            current hour within the simulation day
//   void PushThought(Staff&, uint32_t flags)                 the member's PushThought (draws from rand through GetFeedbackText)
//   char* CleanerText() / char* MechanicText()                     the static text buffers of the two professions
// SetEnv:
//   int32_t FloatToIntWrap(int32_t floatBits)                                script-to-integer conversion of a float is the game's own truncation
//   void Base(Staff&, int32_t key, intptr_t data, int32_t type, int32_t bits)   the staff SetValue
//   void* CreateSprite(const char*) / void ReleaseSprite(void*)    sprite creation from the bank / ReleaseSprite on the global bank
//   void SetAnim(void* sprite, const char* name, float startTime, float* position)   starting the named sprite animation
//   float GameTime()                                               World::m_gameTime
namespace obl::staff {

using object::Cleaner;
using object::Mechanic;
using object::Rep;
using object::SecurityGuard;
using object::Staff;
using script::ScriptValue;
using ui::SpriteInstance;
namespace ex = script_program::exec;

namespace detail {

inline ScriptValue Int(int32_t v)
{
    return {ex::kTypeInt, v};
}
inline ScriptValue Byte(uint8_t v)
{
    return {ex::kTypeByte, v};
} // only the low byte of `bits` is defined (stack garbage above it)
inline ScriptValue Float(float v)
{
    return {ex::kTypeFloat, ex::FloatToBits(v)};
}
// A stored float handed out as it is: the word is copied, so even a signalling NaN keeps its bits.
inline ScriptValue StoredFloat(const float& field)
{
    int32_t bits = 0;
    std::memcpy(&bits, &field, sizeof(bits));
    return {ex::kTypeFloat, bits};
}
inline ScriptValue Truncated(float v)
{
    return Int(FloatToIntWrap(static_cast<double>(v)));
}

// The bin animations of the cleaner and the mechanic: copied into the function's own static buffer, handed out as an *int* holding its address.
inline ScriptValue BufferedText(char* buffer, const char* text)
{
    std::strcpy(buffer, text);
    return Int(guest_script::TextBits(buffer));
}

// `IdleTime` (0x6f): the hours since the last task, the float difference rounded once.
inline ScriptValue IdleTime(float dayTime, float lastTask)
{
    return Float(static_cast<float>(static_cast<double>(dayTime) - static_cast<double>(lastTask)));
}

template<class Env> ScriptValue HomeClosed(const Staff& s, Env& env)
{
    return Byte(env.IsOpen(env.Object(s.m_HomeID)) ? 0 : 1);
}

// `MyDir` (0x38): the facing of the member's own sprite, the script's number as it is (no wrap, unlike the guest's), kept below the count.
template<class Env> void SetOwnDirection(Staff& s, ScriptValue value, Env& env)
{
    const int32_t direction = ex::ToInt(env, value);
    guest_script::SetSpriteDirection(*static_cast<SpriteInstance*>(s.m_sprite), direction);
}

// The sprite attached to a lifeguard or a rep (0x39 `AttachedSprite`, 0x3a `AttachedAnimation`, 0x3b `AttachedDirection`, 0x3e/0x3f offsets): the same
// code in both classes. 0x39..0x3b read the value bits as a name whatever the type says. `NONE` releases the sprite, any other name creates a new one
// over the old pointer (the old one is not released); the animation keeps the name as it is (no Reverse_/Clamp_ prefixes, unlike the guest's) and starts at the
// current game time at the member's position, only when a sprite is attached; `MyDir` makes the attached sprite face where the member does. A key that
// does not apply (no sprite, other name) is consumed all the same: the base class never sees it. Returns whether the key is one of these five.
template<class Member, class Env> bool SetAttached(Member& m, int32_t key, ScriptValue value, Env& env)
{
    const char* text = ex::AsText(value.bits);
    switch (key) {
    case 0x39:
        if (!ascii::EqualsNoCase(text, "NONE")) {
            m.m_attachedSprite = env.CreateSprite(text);
        } else if (m.m_attachedSprite) {
            env.ReleaseSprite(m.m_attachedSprite);
            m.m_attachedSprite = nullptr;
        }
        return true;
    case 0x3a:
        if (m.m_attachedSprite) env.SetAnim(m.m_attachedSprite, text, env.GameTime(), m.m_WorldXYZ);
        return true;
    case 0x3b: {
        auto* attached = static_cast<SpriteInstance*>(m.m_attachedSprite);
        if (ascii::EqualsNoCase(text, "MyDir") && attached)
            guest_script::SetSpriteDirection(*attached, static_cast<SpriteInstance*>(m.m_sprite)->m_direction);
        return true;
    }
    case 0x3e: m.m_AttachedOffsetX = ex::ToInt(env, value); return true;
    case 0x3f: m.m_AttachedOffsetY = ex::ToInt(env, value); return true;
    default: return false;
    }
}

} // namespace detail

// ---- Cleaner (type 11) -------------------------------------------------------------------------------------------------------

inline bool CleanerHandlesGet(int32_t key)
{
    switch (key) {
    case 0x40:
    case 0x6a:
    case 0x6b:
    case 0x6e:
    case 0x6f:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x76:
    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7a:
    case 0xb7:
    case 0xfc: return true;
    default: return false;
    }
}
inline bool CleanerHandlesSet(int32_t key)
{
    return key == 0x38 || key == 0x40 || key == 0x71 || key == 0x78 || key == 0x79 || key == 0x7a;
}

template<class Env> ScriptValue CleanerGetValue(Cleaner& c, int32_t key, intptr_t data, Env& env)
{
    using namespace detail;
    switch (key) {
    case 0x40: return Int(c.m_ActionState);
    case 0x6a: return Byte(c.m_litterFound);
    case 0x6b: return Byte(c.m_binFound);
    case 0x6e: return Byte(c.m_toiletFound);
    case 0x6f: return IdleTime(env.GameDayTime(), c.m_lastTaskTime);
    case 0x71: return Int(c.m_LitterDistance); // stored squared by Set
    case 0x72: return BufferedText(env.CleanerText(), "bin_emptyfullbin_anim");
    case 0x73: return BufferedText(env.CleanerText(), "bin_emptybin_static");
    case 0x76: return Truncated(c.m_BinPos[0]);
    case 0x77: return Truncated(c.m_BinPos[1]);
    case 0x78: return Byte(c.m_urgentClean);
    case 0x79: return Int(c.m_UrgentType);
    case 0x7a: return Int(c.m_walkpoint);
    case 0xb7: return Int(c.m_Wage);
    case 0xfc: return HomeClosed(c, env);
    default: return env.Base(c, key, data);
    }
}

template<class Env> void CleanerSetValue(Cleaner& c, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    const ScriptValue value{type, bits};
    switch (key) {
    case 0x38: detail::SetOwnDirection(c, value, env); return;
    case 0x40: c.m_ActionState = ex::ToInt(env, value); return;
    case 0x71: { // LitterDistance: kept squared for the distance tests, with the 32-bit product's wrap
        const auto d = static_cast<uint32_t>(ex::ToInt(env, value));
        c.m_LitterDistance = static_cast<int32_t>(d * d);
        return;
    }
    case 0x78: c.m_urgentClean = static_cast<uint8_t>(bits); return; // the low byte whatever the type
    case 0x79: c.m_UrgentType = ex::ToInt(env, value); return;
    case 0x7a: c.m_walkpoint = ex::ToInt(env, value); return;
    default: env.Base(c, key, data, type, bits); return;
    }
}

// ---- Mechanic (type 8) -------------------------------------------------------------------------------------------------------

inline bool MechanicHandlesGet(int32_t key)
{
    switch (key) {
    case 0x40:
    case 0x6f:
    case 0x70:
    case 0x73:
    case 0x7a:
    case 0xa9:
    case 0xaa:
    case 0xab:
    case 0xad:
    case 0xae:
    case 0xaf: return true;
    default: return false;
    }
}
inline bool MechanicHandlesSet(int32_t key)
{
    return key == 0x38 || key == 0x40 || key == 0x7a || key == 0xa9 || key == 0xaa;
}

template<class Env> ScriptValue MechanicGetValue(Mechanic& m, int32_t key, intptr_t data, Env& env)
{
    using namespace detail;
    switch (key) {
    case 0x40: return Int(m.m_ActionState);
    case 0x6f: return IdleTime(env.GameDayTime(), m.m_lastTaskTime);
    case 0x70: return Int(m.m_HomeID);
    case 0x73: return BufferedText(env.MechanicText(), "bin_emptybin_static");
    case 0x7a: return Int(m.m_walkpoint);
    case 0xa9: return Byte(m.m_foundRepair);
    case 0xaa: return Byte(m.m_foundBetterJob);
    case 0xab: return Byte(env.Object(m.m_DestObjectID)->m_type == object::ObjectType::Building); // IsBuilding: the move target
    case 0xad: return Int(m.m_repairType);
    case 0xae: return Truncated(m.m_WateringPos[0]);
    case 0xaf: return Truncated(m.m_WateringPos[1]);
    default: return env.Base(m, key, data);
    }
}

template<class Env> void MechanicSetValue(Mechanic& m, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    const ScriptValue value{type, bits};
    switch (key) {
    case 0x38: detail::SetOwnDirection(m, value, env); return;
    case 0x40: m.m_ActionState = ex::ToInt(env, value); return;
    case 0x7a: m.m_walkpoint = ex::ToInt(env, value); return;
    case 0xa9: m.m_foundRepair = static_cast<uint8_t>(bits); return;
    case 0xaa: m.m_foundBetterJob = static_cast<uint8_t>(bits); return;
    default: env.Base(m, key, data, type, bits); return;
    }
}

// ---- Rep (type 9) ------------------------------------------------------------------------------------------------------------

inline bool RepHandlesGet(int32_t key)
{
    switch (key) {
    case 0x00:
    case 0x05:
    case 0x2b:
    case 0x32:
    case 0x3d:
    case 0x40:
    case 0x108:
    case 0x109:
    case 0x10a:
    case 0x10b:
    case 0x10c:
    case 0x10d:
    case 0x10e:
    case 0x10f: return true;
    default: return false;
    }
}
inline bool RepHandlesSet(int32_t key)
{
    switch (key) {
    case 0x32:
    case 0x38:
    case 0x39:
    case 0x3a:
    case 0x3b:
    case 0x3e:
    case 0x3f:
    case 0x40:
    case 0x108:
    case 0x10c: return true;
    default: return false;
    }
}
// The number that creates or releases a sprite in the bank (not repeatable).
inline bool RepSetsExternal(int32_t key)
{
    return key == 0x39;
}

template<class Env> ScriptValue RepGetValue(Rep& r, int32_t key, intptr_t data, Env& env)
{
    using namespace detail;
    const auto* message = static_cast<const messages::Message*>(r.m_currentMessage);
    switch (key) {
    case 0x00: return Int(message->sender); // the base class's three message numbers, answered again here
    case 0x05: return Int(message->data);
    case 0x2b: return Int(message->flags);
    case 0x32: return Byte(r.m_StopAction);
    case 0x3d: return {ex::kTypeText, guest_script::TextBits(r.m_CurrAnimation)};
    case 0x40: return Int(r.m_ActionState);
    case 0x108: return Int(r.m_talking_id);
    case 0x109: { // TimetalkingToGuest: min + (max - min) * whole hours with guests, in 32-bit wrap
        const auto min = static_cast<uint32_t>(r.m_MinTalkTime), max = static_cast<uint32_t>(r.m_MaxTalkTime);
        const auto hours = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(r.m_timeWithGuests)));
        return Int(static_cast<int32_t>((max - min) * hours + min));
    }
    case 0x10a: return StoredFloat(r.m_timeWithGuests);
    case 0x10b: { // PerCentToBoostGuest: 0.8 of the time, at least 0.1 (NaN compares unordered and gives 0.1 too)
        const double boost = static_cast<double>(r.m_timeWithGuests) * static_cast<double>(0.8f); // exact: a product of two floats
        return Float(boost >= static_cast<double>(0.1f) ? static_cast<float>(boost) : 0.1f);
    }
    case 0x10c: return Int(r.m_sentToGuest); // zero-extended to an int, unlike the byte variables
    case 0x10d: return Truncated(r.m_GuestOffSetPosX);
    case 0x10e: return Truncated(r.m_GuestOffSetPosY);
    case 0x10f: return Int(r.m_HomeID);
    default: return env.Base(r, key, data);
    }
}

template<class Env> void RepSetValue(Rep& r, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    const ScriptValue value{type, bits};
    switch (key) {
    case 0x32: r.m_StopAction = static_cast<uint8_t>(bits); return;
    case 0x38: detail::SetOwnDirection(r, value, env); return;
    case 0x40: r.m_ActionState = ex::ToInt(env, value); return;
    case 0x108: r.m_talking_id = ex::ToInt(env, value); return;
    case 0x10c: r.m_sentToGuest = static_cast<uint8_t>(bits); return;
    default:
        if (!detail::SetAttached(r, key, value, env)) env.Base(r, key, data, type, bits);
        return;
    }
}

// ---- SecurityGuard (type 12) -------------------------------------------------------------------------------------------------

inline bool SecurityHandlesGet(int32_t key)
{
    return key == 0x40 || (key >= 0x85 && key <= 0x96 && key != 0x92) || (key >= 0xf9 && key <= 0xfc);
}
inline bool SecurityHandlesSet(int32_t key)
{
    return key == 0x38 || key == 0x40 || key == 0x92;
}

template<class Env> ScriptValue SecurityGetValue(SecurityGuard& g, int32_t key, intptr_t data, Env& env)
{
    using namespace detail;
    const auto Entrance = [&](int axis) {
        return Truncated(static_cast<object::Building*>(env.Object(g.m_HomeID))->m_EntrancePoint[axis]);
    };
    switch (key) {
    case 0x40: return Int(g.m_ActionState);
    case 0x85: { // FoundMisdemeanour: answering "yes" also makes the guard think about it
        const uint8_t found = g.m_misdemeanourFound;
        if (found) env.PushThought(g, 0x20);
        return Byte(found);
    }
    case 0x86: return StoredFloat(g.m_MisdemeanourPos[0]);
    case 0x87: return StoredFloat(g.m_MisdemeanourPos[1]);
    case 0x88: return Byte(g.m_shouldReprimand);
    case 0x89: return Byte(g.m_shouldDetain);
    case 0x8a: return Int(g.m_toleranceClass);
    case 0x8b: return Int(g.m_action);
    case 0x8c: return Int(g.m_offenderId);
    case 0x8d: return Int(g.m_fightWithId);
    case 0x8e: return Int(g.m_numMinorFinesToday);
    case 0x8f: return Int(g.m_numSeriousFinesToday);
    case 0x90: return StoredFloat(g.m_minorFineAmount);
    case 0x91: return StoredFloat(g.m_seriousFineAmount);
    case 0x93: return Byte(g.m_doubleFine);
    case 0x94: return Truncated(g.m_FineOffsetX);
    case 0x95: return Truncated(g.m_FineOffsetY);
    case 0x96: return Int(g.m_FineLevel);
    case 0xf9: return Entrance(0); // HomeEntranceX/Y: the office's entrance point
    case 0xfa: return Entrance(1);
    case 0xfb: return Int(g.m_HomeID);
    case 0xfc: return HomeClosed(g, env);
    default: return env.Base(g, key, data);
    }
}

template<class Env> void SecuritySetValue(SecurityGuard& g, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    const ScriptValue value{type, bits};
    switch (key) {
    case 0x38: detail::SetOwnDirection(g, value, env); return;
    case 0x40: g.m_ActionState = ex::ToInt(env, value); return;
    case 0x92: // PatrolVelocity: the walking speed from the number read as unsigned (zero-extended to 64 bits)
        g.m_maxVel = static_cast<float>(static_cast<uint32_t>(ex::ToInt(env, value)));
        return;
    default: env.Base(g, key, data, type, bits); return;
    }
}

} // namespace obl::staff
