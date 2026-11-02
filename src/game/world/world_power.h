#pragma once

#include "game/tables/building_behavior.h"

#include <cmath>
#include <cstdint>

// world::UpdateBuildingTotals and world::SwitchOffConsumers: the power sums of the world and the "one building
// per frame loses power" rule. The game's objects stay behind an `Env`
// (index into the building list), so the logic compiles and is unit-tested without the DLL.
//
// Arithmetic as in double at 53-bit precision: each sum is a float field, so `double(a) + double(b)` rounded to float
// on the store is a double add and a float store; the building's POWER (an int) goes through a float first.
namespace obl::world_power {

// The world's four power sums (m_powerUse, m_maxPower, m_currentPower, m_powerDemand).
struct Sums {
    float total = 0.0f; // draw of every building
    float installed = 0.0f; // generator capacity (not counted while the generator has a build or demolish job)
    float output = 0.0f; // what the generators deliver now
    float active = 0.0f; // draw of the buildings not switched off for lack of power
};

// `float(double(a) op double(b))` - the sum is computed in double and stored as a float.
inline float AddFloat(float a, float b)
{
    return static_cast<float>(static_cast<double>(a) + static_cast<double>(b));
}
inline float SubFloat(float a, float b)
{
    return static_cast<float>(static_cast<double>(a) - static_cast<double>(b));
}

// Env: int Power(i) (BuildingDef POWER), bool ClosedForPower(i), bool HasBuildJob(i), bool HasDemolishJob(i), int OutputPower(i)
// (the building's power output). Queries are asked in this order: HasBuildJob, then HasDemolishJob only when the first is
// false, then OutputPower, all only for generators (POWER < 0).
template<class Env> Sums SumPower(Env& env, uint32_t count)
{
    Sums sums;
    for (uint32_t i = 0; i < count; ++i) {
        const float power = static_cast<float>(env.Power(i));
        if (power < 0.0f) {
            const float capacity = (env.HasBuildJob(i) || env.HasDemolishJob(i)) ? 0.0f : power;
            sums.installed = SubFloat(sums.installed, capacity);
            sums.output = AddFloat(static_cast<float>(env.OutputPower(i)), sums.output);
        } else {
            if (!env.ClosedForPower(i)) sums.active = AddFloat(power, sums.active);
            sums.total = AddFloat(power, sums.total);
        }
    }
    return sums;
}

// FLT_EPSILON as a double constant in the game's data.
constexpr double kNoGeneratorEpsilon = 1.1920928955078125e-07;

// The two compare tests of world::SwitchOffConsumers. A NaN makes the compare unordered: the first test ("<=" or unordered
// skips) is then false, the others ("<" or unordered) true.
inline bool ShouldShed(const Sums& sums)
{
    const bool demandAboveOutput = sums.active > sums.output;
    const bool outputBelowCapacity = !(sums.output >= sums.installed);
    const bool noGenerators = !(std::fabs(static_cast<double>(sums.installed)) >= kNoGeneratorEpsilon);
    return (demandAboveOutput && outputBelowCapacity) || noGenerators;
}

// Buildings that never lose power: the power-exempt behaviors (building_behavior.h).
inline bool IsShedExempt(BuildingBehavior behavior)
{
    switch (behavior) {
    case BuildingBehavior::MechanicHome:
    case BuildingBehavior::BuilderHome:
    case BuildingBehavior::Hotel:
    case BuildingBehavior::ComplaintsHut:
    case BuildingBehavior::AccomBlock: return true;
    default: return false;
    }
}

// Env additionally: int Rand() (the game's rand), BuildingBehavior Behavior(i), void SwitchOffConsumer(i) (BuildingShedPower).
// Draws one building: `rand() % count` unsigned, once per call, whenever the rule fires; the building loses power when it draws power,
// is not already off, has no build or demolish job and is not exempt. Returns the index shed or -1.
// With no buildings a division by zero (#DE) after `rand()` would fault; here nothing happens (and nothing is drawn).
template<class Env> int SwitchOffConsumers(const Sums& sums, uint32_t count, Env& env)
{
    if (count == 0 || !ShouldShed(sums)) return -1;
    const uint32_t index = static_cast<uint32_t>(env.Rand()) % count;
    if (env.Power(index) <= 0 || env.ClosedForPower(index) || env.HasBuildJob(index) || env.HasDemolishJob(index) ||
            IsShedExempt(env.Behavior(index)))
        return -1;
    env.SwitchOffConsumer(index);
    return static_cast<int>(index);
}

} // namespace obl::world_power
