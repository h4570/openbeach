#pragma once

#include "ferry_state.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>

namespace obl::ferry {

using Schedule = std::list<Boat>;

inline void ResetCursor(Manager& manager)
{
    manager.cursor = manager.schedule.begin();
}

inline void RecordGuestLeaving(Manager& manager, uint32_t type, float happiness, bool leavingEarly)
{
    float& average = manager.guests.fAvgHapp[type];
    average = average + happiness;
    average = average * 0.5f;
    ++(leavingEarly ? manager.guests.iUnhappyLeavers : manager.guests.iHappyLeavers);
}

template<class Env> void AddBoatEvent(Manager& manager, Env& env, const Boat& input)
{
    Boat boat = input;
    if (boat.TimeFlags == 3) {
        boat.Time = env.Hour();
        boat.Day = static_cast<int>(env.Day());
        manager.performance.push_back(boat);
        return;
    }
    auto position = manager.schedule.begin();
    while (position != manager.schedule.end() && !(position->Time >= boat.Time)) ++position;
    const bool first = position == manager.schedule.begin();
    manager.schedule.insert(position, boat);
    if (first) ResetCursor(manager);
}

inline int Truncate(double value)
{
    if (!(value >= -9223372036854775808.0 && value < 9223372036854775808.0)) return 0;
    return static_cast<int>(static_cast<uint32_t>(static_cast<int64_t>(value)));
}

// Takes the next due boat (a pending performance first, else the schedule at its cursor) and fills `out` with its guests. Returns false
// when nothing is due, and moves a day-bound schedule boat past its cursor when its day has not come.
template<class Env> bool PopDueBoat(Manager& manager, Env& env, Arrival& out)
{
    if ((manager.cursor && *manager.cursor == manager.schedule.end()) || (manager.schedule.empty() && manager.performance.empty()))
        return false;
    // A schedule without a cursor is impossible: the first schedule insert and every load set one. A performance boat with another flag
    // than 3 comes only from a load, which sets the cursor too.
    const bool fromPerformance = !manager.performance.empty();
    Schedule& owner = fromPerformance ? manager.performance : manager.schedule;
    const auto node = fromPerformance ? manager.performance.begin() : *manager.cursor;
    const Boat& boat = *node;
    if (!(env.Hour() >= boat.Time)) return false;
    if ((boat.TimeFlags == 0 || boat.TimeFlags == 2) && env.Day() < static_cast<uint32_t>(boat.Day)) {
        manager.cursor = std::next(*manager.cursor);
        return false;
    }
    out.performance = 0;
    out.numGuests = 0;
    std::memset(out.guestTypes, 0, sizeof(out.guestTypes));
    int types = env.GuestCount();
    switch (boat.GuestInfo) {
    case 0:
    case 1: {
        uint32_t count = boat.NumGuests;
        if (boat.GuestInfo == 0) {
            count = count ? static_cast<uint32_t>(env.Rand()) % count : 0;
            if (!count) count = 1;
        }
        out.numGuests = static_cast<int>(count);
        for (uint32_t i = 0; i < count; ++i) ++out.guestTypes[static_cast<uint32_t>(env.Rand()) % types];
        break;
    }
    case 2:
        for (int i = 0; i < types; ++i) {
            if (!boat.GuestKeyword[i][0]) continue;
            int type = env.GuestId(boat.GuestKeyword[i]);
            out.guestTypes[type] = boat.GuestType[i] == 0
                    ? boat.GuestKeywordNumbers[i]
                    : static_cast<int>(static_cast<uint32_t>(env.Rand()) % static_cast<uint32_t>(boat.GuestKeywordNumbers[i]));
            out.numGuests += out.guestTypes[type];
        }
        break;
    case 3: {
        manager.lastLeft = static_cast<int>(manager.guests.iHappyLeavers + manager.guests.iUnhappyLeavers);
        int count = Truncate(static_cast<double>(static_cast<uint32_t>(env.Rating())) * 0.01f * boat.NumGuests);
        if (!count) count = 2;
        out.performance = 1;
        float maleIntensity = 0, femaleIntensity = 0;
        env.Intensities(maleIntensity, femaleIntensity);
        int maleCount = Truncate(static_cast<double>(maleIntensity) / (static_cast<double>(maleIntensity) + femaleIntensity) * count);
        int femaleCount = static_cast<int>(static_cast<uint32_t>(count) - static_cast<uint32_t>(maleCount));
        float sums[2]{};
        for (int i = 0; i < types; ++i) {
            int gender = env.Gender(i) == 0 ? 0 : 1;
            sums[gender] = static_cast<float>(static_cast<double>(sums[gender]) + manager.guests.fAvgHapp[i]);
        }
        const int counts[2] = {maleCount, femaleCount};
        for (int gender = 0; gender < 2; ++gender) {
            if (sums[gender] <= 1e-6f && counts[gender] != 0) {
                for (int i = 0; i < types; ++i) {
                    if (env.Gender(i) != gender) continue;
                    double weight = static_cast<double>(env.Rand()) * std::bit_cast<float>(0x38000100u);
                    manager.guests.fAvgHapp[i] = static_cast<float>(weight);
                    // the unrounded draw is added before spilling the accumulated sum.
                    sums[gender] = static_cast<float>(weight + sums[gender]);
                }
            }
        }
        for (int i = 0; i < types; ++i) {
            int gender = env.Gender(i) == 0 ? 0 : 1;
            int n = Truncate(static_cast<double>(manager.guests.fAvgHapp[i]) / sums[gender] * counts[gender]);
            out.guestTypes[i] = n;
            out.numGuests = static_cast<int>(static_cast<uint32_t>(out.numGuests) + static_cast<uint32_t>(n));
        }
        manager.lastArrived = out.numGuests;
        break;
    }
    }
    manager.guests.iHappyLeavers = manager.guests.iUnhappyLeavers = 0;
    for (int i = 0; i < types; ++i) manager.guests.fAvgHapp[i] = 0;
    const int flags = boat.TimeFlags;
    // The schedule cursor moves past the boat for every flag but 3, also when the boat came from the performances.
    if (flags != 3 && manager.cursor) *manager.cursor = std::next(*manager.cursor);
    if (flags == 0 || flags == 3) {
        if (!fromPerformance && manager.cursor && *manager.cursor == node) *manager.cursor = std::next(node);
        owner.erase(node);
    }
    return true;
}

inline double GetNextBoatTime(const Manager& manager, uint32_t day, float hour, int todayOnly, uint32_t& outDay)
{
    if (manager.schedule.empty()) return -1; // outDay is untouched on the empty-list path.
    float now = static_cast<float>(static_cast<double>(day) * 24 + hour);
    float bestDelta = 0;
    double result = -1;
    bool found = false;
    uint32_t candidateDay = static_cast<uint32_t>(todayOnly);
    outDay = day;
    for (const auto& boat : manager.schedule) {
        if (boat.TimeFlags == 0)
            candidateDay = boat.Day;
        else if (boat.TimeFlags == 1)
            candidateDay = day;
        else if (boat.TimeFlags == 2)
            candidateDay = std::max(day, static_cast<uint32_t>(boat.Day));
        double time = static_cast<double>(candidateDay) * 24 + boat.Time;
        if (time <= now && boat.TimeFlags == 1) {
            candidateDay = day + 1;
            time = static_cast<double>(candidateDay) * 24 + boat.Time;
        }
        if (time > now && (!found || time - now < bestDelta)) {
            result = boat.Time;
            found = true;
            bestDelta = static_cast<float>(time - now);
            outDay = candidateDay;
        }
    }
    if (result > -1 && static_cast<uint8_t>(todayOnly) && outDay != day) result = -1;
    return result;
}

inline double GetBoatTimeToday(const Manager& manager, uint32_t day, int index)
{
    int current = 0;
    for (const auto& boat : manager.schedule) {
        bool valid = boat.TimeFlags == 0 ? static_cast<uint32_t>(boat.Day) == day
                                         : boat.TimeFlags != 2 || static_cast<uint32_t>(boat.Day) <= day;
        if (valid || day == UINT32_MAX) {
            if (current++ == index) return boat.Time;
        }
    }
    return -1;
}

} // namespace obl::ferry
