#pragma once

#include "game/objects/object_staff.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>

// (docs/architecture/objects.md): the Staff methods that touch only the member fields (and, for the sack, the cash and
// the cost counters): no draws, no messages, no calls. The arithmetic is 53-bit: each statement is one
// double operation and one rounding on the store to float, spelled out in double here so the result does not depend on the build's float
// evaluation. Where a value is compared right after it is computed (before the float store is read back) the comparison looks at the double.
namespace obl::staff {

// RecalculateHappinessTarget: the happiness the wage and the overtime lead to, and the rate at which the happiness walks to it.
// The wage is read unsigned (as a 64-bit integer with a zero high word), the overtime hours signed.
inline void RecalculateHappinessTarget(object::Staff& staff)
{
    const double ratio = static_cast<double>(static_cast<uint32_t>(staff.m_Wage)) / static_cast<double>(staff.m_SalaryExpectation) * 0.75;
    staff.m_targetHappiness = static_cast<float>(ratio);
    // `1 < ratio` is false for NaN, so a 0/0 ratio survives to the lower clamp, which is true for NaN.
    if (ratio > 1.0) staff.m_targetHappiness = 1.0f;
    if (!(staff.m_targetHappiness >= 0.0f)) staff.m_targetHappiness = 0.0f;
    const int32_t hours = static_cast<int32_t>(staff.m_hoursWorkedInRow);
    if (hours > 10) {
        staff.m_targetHappiness = static_cast<float>(
                static_cast<double>(staff.m_targetHappiness) - static_cast<double>(hours - 10) * static_cast<double>(0.1f));
    }
    if (staff.m_targetHappiness > 1.0f) staff.m_targetHappiness = 1.0f;
    if (!(staff.m_targetHappiness >= 0.0f)) staff.m_targetHappiness = 0.0f;
    staff.m_happinessChanging = 1;
    staff.m_happinessRate =
            static_cast<float>((static_cast<double>(staff.m_targetHappiness) - static_cast<double>(staff.m_happiness)) * 0.5);
}

// IncrementShiftHours: the `+` button of the shift. Unsigned hours: 24 wraps to 0 (anything above stays).
inline void IncrementShiftHours(int32_t& start, int32_t& end)
{
    for (int32_t* hour : {&start, &end}) {
        uint32_t value = static_cast<uint32_t>(*hour) + 1;
        if (value > 0x17 && value < 0x19) value = 0;
        *hour = static_cast<int32_t>(value);
    }
}
inline void IncrementShiftHours(object::Staff& staff)
{
    IncrementShiftHours(staff.m_StartTime, staff.m_EndTime);
}

// DecrementShiftHours: the `-` button of the shift. A wrap below zero (and every value above 30) becomes 23.
inline void DecrementShiftHours(int32_t& start, int32_t& end)
{
    for (int32_t* hour : {&start, &end}) {
        uint32_t value = static_cast<uint32_t>(*hour) - 1;
        if (value > 0x1e) value = 0x17;
        *hour = static_cast<int32_t>(value);
    }
}
inline void DecrementShiftHours(object::Staff& staff)
{
    DecrementShiftHours(staff.m_StartTime, staff.m_EndTime);
}

// Sack: the severance, ten times the recommended wage of the definition (32-bit product, wrapping), is taken from the
// cash (a signed subtraction) and added to the costs of today and the wages of today (an unsigned 64-bit conversion; the two differ only on overflow).
inline void Sack(object::Staff& staff, auto& world)
{
    const uint32_t severance = static_cast<uint32_t>(staff.m_staffDefinition->m_RecommendedWage) * 10u;
    staff.m_gotTheSack = 1;
    auto& player = *world.m_player;
    player.m_cash = static_cast<float>(static_cast<double>(player.m_cash) - static_cast<double>(static_cast<int32_t>(severance)));
    world.m_costsToday = static_cast<float>(static_cast<double>(severance) + static_cast<double>(world.m_costsToday));
    world.m_wagesCostToday = static_cast<float>(static_cast<double>(severance) + static_cast<double>(world.m_wagesCostToday));
}

} // namespace obl::staff
