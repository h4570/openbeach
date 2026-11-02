#pragma once

#include <cmath>
#include <cstdint>

// The pure decisions of the opening state of a Building (docs/architecture/objects.md): who is closed during which hours
// (ClosedByUserOrPowerDuringHours), what a generator delivers (PowerOutput) and which
// step UpdateOpenState takes. No game memory, no side effects: the game glue in src/game/objects/building_methods.cpp
// reads the fields, calls these and runs the steps.
//
// Comparisons are written with these NaN rules: "A > B" is false for an unordered pair, so a NaN
// hour counts as "not above". The data never has NaN, the form just keeps the branches one to one with the comparison order the tests record.
namespace obl {

// HOURS_<key> open close of the building's definition (WorldParams opening hours): hours of the day as floats; `close < open` wraps
// midnight, `close == open` is round the clock (HOURS_ALLHOURS = 0 0).
struct OpeningHours {
    float open = 0.0f;
    float close = 0.0f;
};

// GetHour: floor of the hour of the day as an integer (`floor`, then a truncating conversion).
inline int32_t WholeHour(float dayTime)
{
    const double hour = std::floor(static_cast<double>(dayTime));
    if (!(hour > -2147483649.0 && hour < 2147483648.0)) return INT32_MIN; // the "integer indefinite" value
    return static_cast<int32_t>(hour);
}

// True for a building that the player closed or that lost power and
// whose opening hours include `dayTime` (the fractional hour of the world clock) - such a building would be open if it were not forced shut
// (the guests' opinion code asks it to tell "closed by the player" from "closed by the clock").
inline bool ClosedByUserOrPowerDuringHours(bool userClosed, bool powerClosed, float dayTime, OpeningHours hours)
{
    if (!userClosed && !powerClosed) return false;
    if (hours.close > hours.open) // a plain window, both ends included
        return !(hours.open > dayTime) && hours.close >= dayTime;
    if (hours.close >= hours.open) // equal: round the clock
        return true;
    // Wrapping over midnight: only the gap (close, open) is outside the hours.
    if (hours.close >= dayTime) return true;
    return !(hours.open > dayTime);
}

// The power a generator (negative `POWER` of the definition) delivers. A closed building (openStatus != 0)
// delivers nothing. The output falls with the wear: |POWER| x 1, 0.75, 0.5, 0.25 for m_RepairState 0..3 (float constants, truncated to int; the analysis lost the multiplications), any other state gives |POWER|. A consumer returns its
// `POWER` unchanged.
inline int32_t PowerOutput(int32_t power, uint32_t openStatus, uint32_t repairState)
{
    if (power >= 0) return power;
    if (openStatus != 0) return 0;
    const int32_t magnitude = static_cast<int32_t>(0u - static_cast<uint32_t>(power));
    if (repairState > 3) return magnitude;
    constexpr float kWear[4] = {1.0f, 0.75f, 0.5f, 0.25f};
    return static_cast<int32_t>(static_cast<double>(magnitude) * kWear[repairState]);
}

// What UpdateOpenState does, decided from the building's flags, the world's power sums and the hour of the day.
enum class OpenStateStep : uint8_t {
    None,
    ClearPowerClosed, // power-closed flag cleared, then UpdateBuildingTotals
    Open, // open status 0, then the staff and events come back (every frame while the hours include `hour`)
    Close, // open status 1, queue cleared, staff and events hidden, MT_BUILDING_CLOSING to the humans (once: only when open)
};

struct OpenStateInput {
    bool closedForPower = false; // shut down by a power shortage
    bool userClosed = false; // closed by the player
    uint32_t repairState = 0; // 3 = broken
    uint32_t openStatus = 0; // 0 open
    int32_t power = 0; // BuildingDef POWER
    float powerDemand = 0.0f; // demand of the buildings that are not shed
    float currentPower = 0.0f; // output of the open generators
    uint32_t hour = 0; // GetHour
    OpeningHours hours;
};

inline OpenStateStep DecideOpenState(const OpenStateInput& in)
{
    if (in.closedForPower) {
        // The building comes back once the output covers its own consumption on top of the demand of the others (a NaN
        // counts as not covering). The sum is a double to keep 53-bit precision.
        const double needed = static_cast<double>(in.power) + static_cast<double>(in.powerDemand);
        return !(needed >= static_cast<double>(in.currentPower)) ? OpenStateStep::ClearPowerClosed : OpenStateStep::None;
    }
    if (in.userClosed || in.repairState == 3) return OpenStateStep::None;
    const float hour = static_cast<float>(in.hour);
    const OpeningHours& hours = in.hours;
    if (!(hours.open > hour) && hours.close > hour) return OpenStateStep::Open;
    const OpenStateStep closeIfOpen = in.openStatus != 0 ? OpenStateStep::None : OpenStateStep::Close;
    if (!(hours.close >= hours.open)) { // wraps midnight (or NaN)
        if (!(hours.open > hour) || hours.close > hour) return OpenStateStep::Open;
        return closeIfOpen;
    }
    if (hours.close == hours.open) // round the clock
        return OpenStateStep::Open;
    return closeIfOpen;
}

} // namespace obl
