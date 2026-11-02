#pragma once

#include "core/float_to_int.h"
#include "game/objects/object_staff.h"

#include <cstdint>
#include <cstring>

// The seven staff menu SetVariable overrides accept a GUI data key and a pointer to its value (
// `key` is the data keyword of the GUI script, `value` points at a 4-byte word the GUI filled). The base class knows one keyword (the wage); each
// profession answers its own keywords by exact `strcmp` and hands every other key to the base class; the builder has none of its own. An unknown key
// ends the game with a FatalError in the base class. Pure logic over object::Staff; what reaches the game goes through an `Env`:
//   env.PushThought(flags)           PushThought (the wage rise / cut thought)
//   env.RecalculateHappiness()       the happiness target recalculation
//   env.Fatal(key)                   FatalError("A script set unrecognized staff variable \"%s\"", key)
//   env.ApplyBeachMode(homeId, mode) the lifeguard tower's (mode 0 = beach open, 1 = closed) on the object homeId
namespace obl::staff {

namespace set_variable {

constexpr const char* kWages = "STAFF_WAGES";
constexpr size_t kWagesPrefix = 11; // strncmp: any key that starts with STAFF_WAGES is the wage
constexpr const char* kBeachOpen = "STAFF_BEACH_OPEN";
constexpr const char* kCleanerToilets = "CLEANER_TOILETS";
constexpr const char* kCleanerBins = "CLEANER_BINS";
constexpr const char* kCleanerLitter = "CLEANER_LITTER";
constexpr const char* kMechanicRepair = "MECHANIC_REPAIR";
constexpr const char* kMechanicVandals = "MECHANIC_VANDALS";
constexpr const char* kMechanicWater = "MECHANIC_WATER";
constexpr const char* kRepGuestTime = "REP_GUEST_TIME";
constexpr const char* kSecurityHarshness = "SECURITY_HARSHNESS";

// PushThought flags of the wage thought: a rise (the new wage is above the old, compared unsigned) or a cut.
constexpr uint32_t kThoughtWageRaised = 0x4000000;
constexpr uint32_t kThoughtWageCut = 0x8000000;
// The least wage the menu can set.
constexpr uint32_t kMinWage = 5;

constexpr uint32_t kMechanicRepairBit = 1u << 0, kMechanicVandalsBit = 1u << 1, kMechanicWaterBit = 1u << 2;

inline bool Is(const char* key, const char* name)
{
    return std::strcmp(key, name) == 0;
}
inline uint32_t Word(const void* value)
{
    uint32_t word;
    std::memcpy(&word, value, sizeof(word));
    return word;
}
inline uint8_t Byte(const void* value)
{
    return *static_cast<const uint8_t*>(value);
}

// The tolerance class of the guard: the slider float times 5, truncated toward zero, read as unsigned: 0..1 -> 0, 2 -> 1, anything else (3, 4, beyond
// the slider, negative, NaN is 0) -> 2.
inline int32_t ToleranceClass(float slider)
{
    const uint32_t scaled = static_cast<uint32_t>(FloatToIntWrap(static_cast<double>(slider) * 5.0));
    switch (scaled) {
    case 0:
    case 1: return 0;
    case 2: return 1;
    default: return 2;
    }
}

} // namespace set_variable

template<class Env> void SetVariable(object::Staff& member, const char* key, const void* value, Env& env)
{
    using namespace set_variable;
    if (std::strncmp(key, kWages, kWagesPrefix) != 0) {
        env.Fatal(key);
        return;
    }
    const uint32_t wage = Word(value);
    env.PushThought(static_cast<uint32_t>(member.m_Wage) < wage ? kThoughtWageRaised : kThoughtWageCut);
    member.m_Wage = static_cast<int32_t>(wage);
    if (wage < kMinWage) member.m_Wage = static_cast<int32_t>(kMinWage);
    env.RecalculateHappiness();
}

template<class Env> void SetVariableCleaner(object::Cleaner& member, const char* key, const void* value, Env& env)
{
    using namespace set_variable;
    if (Is(key, kCleanerToilets))
        member.m_cleanToilets = Byte(value);
    else if (Is(key, kCleanerBins))
        member.m_cleanBins = Byte(value);
    else if (Is(key, kCleanerLitter))
        member.m_cleanLitter = Byte(value);
    else
        SetVariable(member, key, value, env);
}

template<class Env> void SetVariableLifeguard(object::Lifeguard& member, const char* key, const void* value, Env& env)
{
    using namespace set_variable;
    if (!Is(key, kBeachOpen)) {
        SetVariable(member, key, value, env);
        return;
    }
    env.ApplyBeachMode(member.m_HomeID, Word(value) != 0 ? 0u : 1u);
}

template<class Env> void SetVariableMechanic(object::Mechanic& member, const char* key, const void* value, Env& env)
{
    using namespace set_variable;
    uint32_t bit = 0;
    if (Is(key, kMechanicVandals))
        bit = kMechanicVandalsBit;
    else if (Is(key, kMechanicRepair))
        bit = kMechanicRepairBit;
    else if (Is(key, kMechanicWater))
        bit = kMechanicWaterBit;
    else {
        SetVariable(member, key, value, env);
        return;
    }
    // Bit n = bit 0 of the value byte (the masked byte is shifted into place).
    member.m_JobMask = (member.m_JobMask & ~bit) | (static_cast<uint32_t>(Byte(value) & 1) ? bit : 0u);
}

template<class Env> void SetVariableRep(object::Rep& member, const char* key, const void* value, Env& env)
{
    using namespace set_variable;
    if (!Is(key, kRepGuestTime)) {
        SetVariable(member, key, value, env);
        return;
    }
    std::memcpy(&member.m_timeWithGuests, value, sizeof(float));
}

template<class Env> void SetVariableSecurity(object::SecurityGuard& member, const char* key, const void* value, Env& env)
{
    using namespace set_variable;
    if (!Is(key, kSecurityHarshness)) {
        SetVariable(member, key, value, env);
        return;
    }
    // The slider value is quieted as a float load and store would do it: a signaling NaN becomes a quiet one.
    uint32_t bits = Word(value);
    if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0) bits |= 0x00400000u;
    std::memcpy(&member.m_tolerance, &bits, sizeof(float));
    member.m_toleranceClass = ToleranceClass(member.m_tolerance);
}

} // namespace obl::staff
