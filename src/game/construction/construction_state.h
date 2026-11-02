#pragma once

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <vector>

// World-owned building sites and the table of up to 50 builders.
namespace obl::construction {

constexpr int kWorkPoints = 10; // work points per job, one per builder slot
constexpr int kMaxBuilders = 50;

// A fence the job still has to put up (a "pending hoarding"). Only a loaded save fills them; the game spawns them from the back.
struct Hoarding {
    int32_t m_direction = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t m_savedMetadata = 0;
    bool operator==(const Hoarding&) const = default;
};

// Where a builder stands to work on the site (world coordinates as integers) and which way it faces.
struct WorkPoint {
    uint32_t x = 0;
    uint32_t y = 0;
    int32_t direction = 0;
    bool operator==(const WorkPoint&) const = default;
};

// One building site. Bytes, not bools, for the flags: the callers read the raw byte (IsStarted answers it unchanged) and a loaded save
// keeps whatever value it had.
struct Job {
    int32_t m_buildingId = 0; // the building's object id
    int32_t m_builderCount = 0;
    std::vector<Hoarding> hoardings; // pending fences; the last one is spawned first
    float m_constructionStart = 0;
    float m_demolitionStart = 0;
    float m_constructionRemaining = 0; // remaining construction work (starts at the definition's build time)
    float m_demolitionRemaining = 0; // remaining demolition work
    float m_constructionProgress = 0;
    float m_demolitionProgress = 0;
    uint8_t m_underConstruction = 0;
    uint8_t m_underDemolition = 0;
    uint8_t m_started = 0;
    uint8_t m_finished = 0;
    // Buildings without a footprint keep zero work points; saves and snapshots still write all ten.
    bool hasWorkPoints = false;
    std::array<WorkPoint, kWorkPoints> workPoints{};
    std::array<int32_t, kWorkPoints> m_workPointOwners{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}; // builder token per slot, -1 = free
    bool operator==(const Job&) const = default;
};

// One entry of the builder table (saved as ID, Status, Location, m_buildingId, ManuallyAssigned).
struct Builder {
    int32_t m_builderId = 0;
    int32_t m_status = 0; // 0 available, 1 not (ReallocateBuilders)
    uint32_t m_location = 0; // the builder script's BuilderLocation state
    int32_t m_buildingId = 0; // the building the builder works on, -1 = none
    uint8_t m_manuallyAssigned = 0;
    bool operator==(const Builder&) const = default;
};

// Jobs are owned through pointers: callers hold a Job* across calls that add jobs (StartBuild, the live walks of ReallocateBuilders).
using JobList = std::list<std::unique_ptr<Job>>;

struct Manager {
    JobList jobs;
    int32_t m_constructionCount = 0; // started minus finished jobs; DeleteJob never lowers it, so it is not the list length
    std::array<Builder, kMaxBuilders> m_builders{};
    int32_t m_builderCount = 0; // entries 0..N-1 of m_builders are used
};

} // namespace obl::construction
