#pragma once

#include <cstdint>
#include <list>
#include <optional>

namespace obl::ferry {

constexpr int kGuestTypes = 20; // the guest types of the world's parameters (the arrays of a boat and of the leavers' averages)
constexpr int kKeywordSize = 64; // a guest keyword with its terminating zero, as the save reads it

// One ferry arrival; the schedule and the pending performances hold the same record. Keywords stay fixed buffers because the save and the
// snapshot read them as they were written (a keyword is the text up to its zero).
struct Boat {
    int GuestInfo = 0;
    int GuestType[kGuestTypes] = {};
    char GuestKeyword[kGuestTypes][kKeywordSize] = {};
    int GuestKeywordNumbers[kGuestTypes] = {};
    int NumGuests = 0;
    float Time = 0;
    int Day = 0;
    int TimeFlags = 0;
};

// The guests who left the world by boat since the last arrival: a running average of their happiness per type and the two counts.
struct GuestsLeft {
    float fAvgHapp[kGuestTypes] = {};
    uint32_t iHappyLeavers = 0;
    uint32_t iUnhappyLeavers = 0;
};

// The guests of one boat arrival.
struct Arrival {
    uint8_t performance = 0; // 1 for a performance, whose guests come from the leavers' averages
    int numGuests = 0;
    int guestTypes[kGuestTypes] = {};
};

struct Manager {
    std::list<Boat> schedule; // the daily and one-off boats, ordered by time
    // The next boat of the schedule. Empty until the first schedule insert (or a load) points it; end() is past the last boat.
    std::optional<std::list<Boat>::iterator> cursor;
    std::list<Boat> performance; // the boats due now (the performances and the ones that were added with TimeFlags 3)
    GuestsLeft guests;
    int lastArrived = 0; // guests of the last performance; not saved (the save keeps only the leavers' sums)
    int lastLeft = 0; // leavers counted for the last performance; not saved
};

} // namespace obl::ferry
