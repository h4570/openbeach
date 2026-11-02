#pragma once

#include "core/float_to_int.h"
#include "game/objects/staff_profession_script_values.h"

#include <cmath>
#include <cstdint>

// The script variables of the other two staff professions, the overrides of GetValue and SetValue in front of the staff GetValue / SetValue
// (same shape as src/game/objects/staff_profession_script_values.h, staff script values, docs/architecture/objects.md):
//   the lifeguard's GetValue / SetValue and the builder's GetValue / SetValue.
// Both switch over the number as unsigned; every other number is one call of the staff GetValue / SetValue. Pure logic
// over the layouts; what reaches the game goes through an `Env` (game side: src/game/objects/staff_values.cpp).
//
// Lifeguard GetEnv:
//   object::Base* Object(int32_t id)                         the object manager's lookup (callers expect the object to exist)
//   bool IsOpen(object::Base*)                               IsOpen of the building
//   char* LifeguardText()                                          the one static text buffer of the three animation names ()
//   ScriptValue Base(Staff&, int32_t key, intptr_t data)      the staff GetValue
// Lifeguard SetEnv: FloatToIntWrap, Base, CreateSprite / ReleaseSprite / SetAnim / GameTime (the attached sprite, as the rep's), PushThought.
//
// Builder GetEnv (the construction manager is `World::m_constructionManager`, all of these are its game functions):
//   object::Base* Object(int32_t id)
//   float GameDayTime()                                            current hour within the simulation day
//   int32_t CountJobs(int32_t mode)                                counting construction jobs
//   int32_t AssignNearestJob(int32_t builder, const float pos[3], int32_t mode, uint32_t* type)   assigning the nearest builder job
//   int32_t WorkDirection(int32_t building, int32_t token)         the work direction
//   int32_t BuilderLocation(int32_t builder)                       the builder location
//   void BuildEntryPoint(int32_t building, int32_t token, const float pos[3], float out[3])   the builder entry point
//   bool HasBuildJob(Entity*) / bool HasDemolishJob(Entity*)   the building's HasBuildJob / HasDemolishJob
//   int32_t Whistling()                                            the global g_NumBuildersWhistling, shared by all builders
//   ScriptValue Base(Staff&, int32_t key, intptr_t data)
// Builder SetEnv: FloatToIntWrap, Base, int32_t& Whistling(), void SetBuilderLocation(int32_t builder, int32_t location), PushThought.
namespace obl::staff {

using object::Builder;
using object::Lifeguard;

// ---- Lifeguard (type 7) ------------------------------------------------------------------------------------------------------

inline bool LifeguardHandlesGet(int32_t key)
{
    switch (key) {
    case 0x32:
    case 0x3d:
    case 0x40:
    case 0x97:
    case 0x98:
    case 0x99:
    case 0x9a:
    case 0x9b:
    case 0x9c:
    case 0x9d:
    case 0x9e:
    case 0x9f:
    case 0xa0:
    case 0xa1:
    case 0xa2:
    case 0xa3:
    case 0xa4:
    case 0xa6:
    case 0xa7:
    case 0xa8:
    case 0xe0:
    case 0xfc: return true;
    default: return false;
    }
}
inline bool LifeguardHandlesSet(int32_t key)
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
    case 0x97:
    case 0x98:
    case 0x99:
    case 0x9a:
    case 0x9b:
    case 0x9c:
    case 0x9d:
    case 0xa4:
    case 0xa5: return true;
    default: return false;
    }
}
// The number that creates or releases a sprite in the bank (not repeatable).
inline bool LifeguardSetsExternal(int32_t key)
{
    return key == 0x39;
}
// Setting `Saving` to anything but 0 makes the lifeguard think about it (a draw from rand).
inline bool LifeguardSetMayDraw(int32_t key, int32_t bits)
{
    return key == 0x99 && (bits & 0xff) != 0;
}

namespace detail {

// `DistanceToSender` (0xa0): the 3D distance to the guest being saved, this - other per axis, summed z, y, x (this order, in 53 bits, keeps the results bit-exact).
inline float DistanceBetween(const float employee[3], const float other[3])
{
    const double dx = static_cast<double>(employee[0]) - static_cast<double>(other[0]);
    const double dy = static_cast<double>(employee[1]) - static_cast<double>(other[1]);
    const double dz = static_cast<double>(employee[2]) - static_cast<double>(other[2]);
    return static_cast<float>(std::sqrt(dz * dz + dy * dy + dx * dx));
}

} // namespace detail

template<class Env> ScriptValue LifeguardGetValue(Lifeguard& l, int32_t key, intptr_t data, Env& env)
{
    using namespace detail;
    const auto Saved = [&]() -> const object::Base& { return *env.Object(l.m_saving_id); };
    switch (key) {
    case 0x32: return Byte(l.m_StopAction);
    case 0x3d: return {ex::kTypeText, guest_script::TextBits(l.m_CurrAnimation)};
    case 0x40: return Int(l.m_ActionState);
    case 0x97: return StoredFloat(l.m_ExitPoint[0]);
    case 0x98: return StoredFloat(l.m_ExitPoint[1]);
    case 0x99: return Byte(l.m_saving);
    case 0x9a: return Int(l.m_saving_id);
    case 0x9b: return Byte(l.m_inSea);
    case 0x9c: return Byte(l.m_enteringSea);
    case 0x9d: return Int(l.m_timesPathBlocked);
    case 0x9e: return StoredFloat(Saved().m_WorldXYZ[0]); // SavingX/Y: the position of the guest being saved, copied as words
    case 0x9f: return StoredFloat(Saved().m_WorldXYZ[1]);
    case 0xa0: return Float(DistanceBetween(l.m_WorldXYZ, Saved().m_WorldXYZ));
    case 0xa1: return BufferedText(env.LifeguardText(), "rescue");
    case 0xa2: return BufferedText(env.LifeguardText(), "rescue_swim");
    case 0xa3: return Byte(l.m_inMyArea);
    case 0xa4: return Byte(l.m_AtHotel);
    case 0xa6: return Int(600); // SavingDistance
    case 0xa7: return Byte(l.m_RescueThenDeath);
    case 0xa8: return Byte(static_cast<const object::Guest&>(Saved()).m_ActionState == 1); // GuestAlreadyBeingSaved
    case 0xe0: return BufferedText(env.LifeguardText(), "D_STAND_SAD");
    case 0xfc: return HomeClosed(l, env);
    default: return env.Base(l, key, data);
    }
}

template<class Env> void LifeguardSetValue(Lifeguard& l, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    const ScriptValue value{type, bits};
    switch (key) {
    case 0x32: l.m_StopAction = static_cast<uint8_t>(bits); return;
    case 0x38: detail::SetOwnDirection(l, value, env); return;
    case 0x40: l.m_ActionState = ex::ToInt(env, value); return;
    case 0x97: l.m_ExitPoint[0] = ex::ToFloatNarrow(value); return;
    case 0x98: l.m_ExitPoint[1] = ex::ToFloatNarrow(value); return;
    case 0x99: // Saving: a "yes" also makes the lifeguard think about it
        l.m_saving = static_cast<uint8_t>(bits);
        if (l.m_saving) env.PushThought(l, 0x40);
        return;
    case 0x9a: l.m_saving_id = ex::ToInt(env, value); return;
    case 0x9b: l.m_inSea = static_cast<uint8_t>(bits); return;
    case 0x9c: l.m_enteringSea = static_cast<uint8_t>(bits); return;
    case 0x9d: l.m_timesPathBlocked = ex::ToInt(env, value); return;
    case 0xa4: l.m_AtHotel = static_cast<uint8_t>(bits); return;
    case 0xa5: l.m_DrawGuest = static_cast<uint8_t>(bits); return;
    default:
        if (!detail::SetAttached(l, key, value, env)) env.Base(l, key, data, type, bits);
        return;
    }
}

// ---- Builder (type 10) -------------------------------------------------------------------------------------------------------

inline bool BuilderHandlesGet(int32_t key)
{
    switch (key) {
    case 0x40:
    case 0x6f:
    case 0x70:
    case 0xb0:
    case 0xb1:
    case 0xb2:
    case 0xb3:
    case 0xb4:
    case 0xb5:
    case 0xb6:
    case 0xf9:
    case 0xfa:
    case 0xfd:
    case 0xfe:
    case 0xff:
    case 0x100:
    case 0x101:
    case 0x102:
    case 0x103:
    case 0x104:
    case 0x106:
    case 0x107: return true;
    default: return false;
    }
}
inline bool BuilderHandlesSet(int32_t key)
{
    return key == 0x38 || key == 0x40 || key == 0xff || key == 0x100 || key == 0x101 || key == 0x104 || key == 0x106 || key == 0x107;
}
// `LookForNearestJob` assigns the builder to a job in the construction manager (once, not repeatable).
inline bool BuilderGetsExternal(int32_t key)
{
    return key == 0xb0;
}
// `BuilderLocation` = 4 makes the builder think about it (a draw from rand).
inline bool BuilderSetMayDraw(int32_t key, int32_t location)
{
    return key == 0x104 && location == 4;
}

// The default facings of the builders by `BuilderNumber`: `BuildFacing` when the manager has no direction for the job (first ten), `RestFacing` (the next
// ten). The index is not checked; the numbers the game gives are 0..9 (ten builders at most).
inline int32_t BuilderDefaultFacing(int32_t index)
{
    static const int32_t kTable[20] = {2, 6, 7, 3, 0, 1, 2, 4, 3, 1, 4, 6, 0, 2, 7, 3, 4, 1, 7, 3};
    return index >= 0 && index < 20 ? kTable[index] : 0;
}

template<class Env> ScriptValue BuilderGetValue(Builder& b, int32_t key, intptr_t data, Env& env)
{
    using namespace detail;
    const auto EntryPoint = [&](int axis) {
        float entry[3];
        env.BuildEntryPoint(b.m_DestObjectID, b.m_BuilderNumber, b.m_WorldXYZ, entry);
        return Truncated(entry[axis]);
    };
    switch (key) {
    case 0x40: return Int(b.m_ActionState);
    case 0x6f: return IdleTime(env.GameDayTime(), b.m_lastTaskTime);
    case 0x70: return Int(b.m_HomeID);
    case 0xb0: { // LookForNearestJob: without any job nothing is asked; otherwise the nearest job is the builder's target (and its type, unless none)
        if (env.CountJobs(2) == 0) return Byte(0);
        uint32_t type = 0;
        b.m_DestObjectID = env.AssignNearestJob(static_cast<int32_t>(b.m_id), b.m_WorldXYZ, 2, &type);
        if (b.m_DestObjectID == -1) return Byte(0);
        b.m_JobType = static_cast<int32_t>(type);
        return Byte(1);
    }
    case 0xb1: return Int(b.m_JobType);
    case 0xb2: return EntryPoint(0);
    case 0xb3: return EntryPoint(1);
    case 0xb4: { // BuildFacing: from the job, or the builder's default when the manager gives none
        const int32_t facing = env.WorkDirection(b.m_DestObjectID, b.m_BuilderNumber);
        return Int(facing != 0 ? facing : BuilderDefaultFacing(b.m_BuilderNumber));
    }
    case 0xb5: return Int(BuilderDefaultFacing(b.m_BuilderNumber + 10)); // RestFacing
    case 0xb6: return Int(b.m_BuilderNumber);
    case 0xf9: return Truncated(b.m_HomePoint[0]); // HomeEntranceX/Y: the builder's saved home point
    case 0xfa: return Truncated(b.m_HomePoint[1]);
    case 0xfd: return Int(b.m_NearestValidTileX);
    case 0xfe: return Int(b.m_NearestValidTileY);
    case 0xff: return Int(b.m_NumTimesPlayedAnimInRow);
    case 0x100: return Int(b.m_WaitTillMorning);
    case 0x101: return Int(env.Whistling());
    case 0x102: { // AmIDemolishing
        object::Base* job = env.Object(b.m_DestObjectID);
        return Byte(job && env.HasDemolishJob(job));
    }
    case 0x103: { // IsJobStillValid: the target still has a demolition or a building job
        object::Base* job = env.Object(b.m_DestObjectID);
        return Byte(job && (env.HasDemolishJob(job) || env.HasBuildJob(job)));
    }
    case 0x104: return Int(env.BuilderLocation(static_cast<int32_t>(b.m_id)));
    case 0x106: return Int(b.m_Night);
    case 0x107: return Int(b.m_InHut);
    default: return env.Base(b, key, data);
    }
}

template<class Env> void BuilderSetValue(Builder& b, int32_t key, intptr_t data, int32_t type, int32_t bits, Env& env)
{
    const ScriptValue value{type, bits};
    switch (key) {
    case 0x38: detail::SetOwnDirection(b, value, env); return;
    case 0x40: b.m_ActionState = ex::ToInt(env, value); return;
    case 0xff: b.m_NumTimesPlayedAnimInRow = ex::ToInt(env, value); return;
    case 0x100: b.m_WaitTillMorning = ex::ToInt(env, value); return;
    case 0x101: env.Whistling() = ex::ToInt(env, value); return;
    case 0x104: { // BuilderLocation: kept, and told to the manager when it is one of its six places; at 4 (in the hut) the builder thinks about its job
        const int32_t location = ex::ToInt(env, value);
        b.m_BuilderLocation = location;
        if (location >= 0 && location <= 5) env.SetBuilderLocation(static_cast<int32_t>(b.m_id), location);
        if (location == 4) env.PushThought(b, b.m_JobType == 0 ? 8 : 0x10);
        return;
    }
    case 0x106: b.m_Night = ex::ToInt(env, value); return;
    case 0x107: b.m_InHut = ex::ToInt(env, value); return;
    default: env.Base(b, key, data, type, bits); return;
    }
}

} // namespace obl::staff
