#pragma once

#include <cstdint>
#include <list>

namespace obl::repair {

struct Job {
    int32_t id = 0; // object id of the building or scenery
    uint32_t calls = 0; // repeated requests while it waited
    float distance = 0; // squared distance of the last scan that measured it (scratch, saved but not in the snapshot)
    float timeCalled = 0; // World::m_gameTime of the first request
    uint8_t urgent = 0;
    int32_t type = 0; // kTypeInspection, kTypeScenery, kTypeBreakdown (repair_logic.h)
    bool operator==(const Job&) const = default;
};

// A job a mechanic (or gardener) has claimed.
struct ActiveJob {
    int32_t id = 0;
    uint8_t repairing = 0; // the work has started
    uint8_t urgent = 0;
    int32_t type = 0;
    bool operator==(const ActiveJob&) const = default;
};

using JobList = std::list<Job>;
using ActiveList = std::list<ActiveJob>;

struct Manager {
    JobList buildingJobs; // inspections and breakdowns
    JobList sceneryJobs;
    ActiveList activeJobs;
    // Only the save and the scenario script variables (0xba, 0xbb) read the first two; nothing in the manager changes them.
    int32_t breakdownCount = 0;
    int32_t vandalismCount = 0;
    int32_t activeCount = 0; // grows with every claim, never decremented
};

// A ScanJobs result: the nearest candidate per slot and the list it is in.
struct Scan {
    uint8_t urgentFound = 0, kind1Found = 0, kind0Found = 0;
    Job* urgent = nullptr;
    Job* kind1 = nullptr;
    Job* kind0 = nullptr;
    JobList* urgentList = nullptr;
    JobList* kind1List = nullptr;
    JobList* kind0List = nullptr;
};

} // namespace obl::repair
