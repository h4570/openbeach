#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace obl::disaster {

// One scheduled disaster as WORLD_SCRIPT writes it. `target` is the name of a lightning target.
struct Event {
    int type = 0;
    int rect[4] = {};
    float duration = 0;
    float time = 0;
    uint32_t day = 0;
    int scale = 0;
    float disasterValue = 0;
    int beerType = 0;
    int replacementBeer = 0;
    float replacementStrength = 0;
    std::optional<std::string> target;
    friend bool operator==(const Event&, const Event&) = default;
};

struct Manager {
    std::vector<Event> schedule; // ordered by day and time; the world script fills it
    std::vector<int> active; // schedule indices of the running events
    int nextEvent = 0; // the next schedule index to start
    int numEvents = 0;
    int numCurEvents = 0; // the running count
    int dayOffset = 0;
    int curDayOffset = 0;
    friend bool operator==(const Manager&, const Manager&) = default;
};

} // namespace obl::disaster
