#pragma once

#include <cstdint>
#include <list>

namespace obl::cleaning {

struct Job {
    int32_t id = 0; // object id of the bin, litter or toilet
    uint32_t calls = 0; // repeated requests while it waited
    float timeCalled = 0; // World::m_gameTime of the request; 0 for a request that has no time stamp
    uint8_t urgent = 0;
    bool operator==(const Job&) const = default;
};

// A job a cleaner has claimed.
struct ActiveJob {
    int32_t id = 0;
    uint8_t cleaning = 0; // the cleaner has reached the object
    uint8_t urgent = 0;
    bool operator==(const ActiveJob&) const = default;
};

using JobList = std::list<Job>;
using ActiveList = std::list<ActiveJob>;

struct Manager {
    JobList jobs;
    int32_t cleaningCount = 0;
    ActiveList activeJobs;
    int32_t activeCount = 0; // +1 per claim, -1 per release
};

} // namespace obl::cleaning
