#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace obl::feedback {

constexpr int kFamilies = 26; // commodity families of the guests' complaint counters (commodity::kFamilies)
constexpr int kMaxSuppressed = 100;

struct Text {
    bool wide = false;
    std::u16string wideText;
    std::string key;

    static Text Key(const char* key)
    {
        Text text;
        text.key = key;
        return text;
    }
    static Text Wide(const char16_t* wide)
    {
        Text text;
        text.wide = true;
        text.wideText = wide;
        return text;
    }
    bool operator==(const Text&) const = default;
};

struct Notice {
    int32_t objectId = 0; // -1/-2: no "go to" button
    Text text;
    bool operator==(const Notice&) const = default;
};

struct HistoryEntry {
    int32_t objectId = 0;
    int32_t day = 0;
    float hour = 0;
    Text text;
    bool operator==(const HistoryEntry&) const = default;
};

struct Manager {
    std::deque<Notice> pending; // waiting for a free balloon, at most 16
    std::deque<HistoryEntry> history; // the last six shown ones
    size_t historyCursor = 0; // BeginHistory/NextHistory; not saved

    // Staff messages already announced, one bit mask per staff class (feedback_logic.h StaffMask).
    uint32_t lifeGuardMessages = 0;
    uint32_t cleanerMessages = 0;
    uint32_t securityGuardMessages = 0;
    uint32_t mechanicMessages = 0;
    uint32_t repMessages = 0;
    uint32_t builderMessages = 0;
    uint32_t staffMessages = 0;

    // Silenced message types in the order the scenario named them. A fixed array with a count, not a set: the save writes
    // all 100 slots and a loaded save may hold duplicates or values past the count, which must survive a round trip.
    uint32_t suppressedCount = 0;
    std::array<uint32_t, kMaxSuppressed> suppressed{};
    uint8_t suppressWorld = 0; // bytes, not bools: a loaded save keeps whatever value it had
    uint8_t suppressObject = 0;
    float lastReviewHour = 25; // hour of the last complaint review, 25 until the first one
};

} // namespace obl::feedback
