#pragma once

#include "construction_queries.h"
#include "game/construction/construction_state.h"
#include "game/messages/message_state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Job and builder mutations keep assignments, job counts and work-point claims in sync.
// See docs/specs/construction.md for their contracts.
namespace obl::construction {

inline bool SetStarted(Manager& manager, int buildingId, uint8_t started)
{
    Job* job = FindJob(manager, buildingId);
    if (!job) return false;
    job->m_started = started;
    return true;
}

// The first builder entry 0..N-1 with the builder's id, null when none (the lookup of Get/SetBuilderLocation).
inline Builder* FindBuilder(Manager& manager, int builderId)
{
    const int builders = std::min(manager.m_builderCount, kMaxBuilders);
    for (int i = 0; i < builders; ++i)
        if (manager.m_builders[i].m_builderId == builderId) return &manager.m_builders[i];
    return nullptr;
}

// The BuilderLocation state of the builder's entry (FindBuilder); a missing entry writes nothing and answers false.
inline bool SetBuilderLocation(Manager& manager, int builderId, int location)
{
    Builder* builder = FindBuilder(manager, builderId);
    if (!builder) return false;
    builder->m_location = static_cast<uint32_t>(location);
    return true;
}

// Job lifecycle and builder assignments.

// What construction::ConstructJob reads from the building's definition (BuildingDef via Building::m_definition).
struct JobDefinition {
    int buildTime;
    int demolishTime;
    bool hasFootprint;
};

// Initialize every work point and leave owner tokens unclaimed. A footprint may supply fewer usable points than the builder limit,
// so unused slots must also have deterministic coordinates: BuildEntryPoint uses the builder's fallback for an empty slot.
// Without a footprint, hasWorkPoints prevents readers from treating the zeroed points as work locations.
inline void ConstructJob(Job& job, int buildingId, const JobDefinition& def)
{
    job = Job{};
    job.m_buildingId = buildingId;
    job.m_constructionRemaining = static_cast<float>(static_cast<uint32_t>(def.buildTime));
    job.m_demolitionRemaining = static_cast<float>(static_cast<uint32_t>(def.demolishTime));
    job.hasWorkPoints = def.hasFootprint;
}

// A new job at the end of the list (AppendJob with the sentinel: StartBuild, StartDemolish and Load all append).
inline Job& AppendJob(Manager& manager, int buildingId, const JobDefinition& def)
{
    Job& job = *manager.jobs.emplace_back(std::make_unique<Job>());
    ConstructJob(job, buildingId, def);
    return job;
}

// construction::DeleteJob: removes the first job with the building id. The job count is not touched. False when no job
// matches.
inline bool DeleteJob(Manager& manager, int buildingId)
{
    const auto it =
            std::find_if(manager.jobs.begin(), manager.jobs.end(), [&](const auto& job) { return job->m_buildingId == buildingId; });
    if (it == manager.jobs.end()) return false;
    manager.jobs.erase(it);
    return true;
}

template<class Env> void ReleaseBuilder(Manager& manager, int count, int id, Env& env)
{
    int token = 0;
    const bool haveObject = env.BuilderToken(id, &token);
    for (int i = 0; i < count; ++i) {
        if (manager.m_builders[i].m_builderId != id) continue;
        if (Job* job = FindJob(manager, manager.m_builders[i].m_buildingId)) {
            --job->m_builderCount;
            if (haveObject) {
                for (int& owner : job->m_workPointOwners) {
                    if (owner != token) continue;
                    owner = -1;
                    break;
                }
            }
        }
        for (int j = 0; j < count; ++j) {
            if (manager.m_builders[j].m_builderId != id) continue;
            manager.m_builders[j].m_buildingId = -1;
            manager.m_builders[j].m_manuallyAssigned = 0;
        }
    }
}

// New builders start without an assignment and trigger reallocation. Refuse a full table before changing the count or assignments.
template<class Env> bool AddBuilder(Manager& manager, int builderId, Env& env)
{
    const int count = manager.m_builderCount;
    if (count < 0 || count >= kMaxBuilders) return false;
    Builder& entry = manager.m_builders[count];
    entry.m_builderId = builderId;
    entry.m_status = 0;
    entry.m_location = 0;
    entry.m_buildingId = -1;
    entry.m_manuallyAssigned = 0;
    manager.m_builderCount = count + 1;
    env.Reallocate();
    return true;
}

// Release the selected builder's job and work-point claim, compact the table, then reallocate the remaining builders.
// Callers must register the builder first: a missing id selects the unused entry after the active prefix and still decreases
// the count. Reject empty or oversized tables. If the builder object is absent, skip its work-point release.
template<class Env> bool RemoveBuilder(Manager& manager, int builderId, Env& env)
{
    const int count = manager.m_builderCount;
    if (count <= 0 || count > kMaxBuilders) return false;
    int index = 0;
    while (index < count && manager.m_builders[index].m_builderId != builderId) ++index;
    const int id = index < kMaxBuilders ? manager.m_builders[index].m_builderId : count;
    ReleaseBuilder(manager, count, id, env);
    for (int i = index; i < count - 1; ++i) manager.m_builders[i] = manager.m_builders[i + 1];
    manager.m_builderCount = count - 1;
    env.Reallocate();
    return true;
}

inline bool JobHasRoom(int assigned, float limit)
{
    return !(static_cast<double>(static_cast<uint32_t>(assigned)) >= static_cast<double>(limit));
}

// |a - b| as 32-bit subtraction, then `(v ^ sign) - sign`.
inline int32_t DistanceAbs(int32_t a, int32_t b)
{
    const uint32_t difference = static_cast<uint32_t>(a) - static_cast<uint32_t>(b);
    const uint32_t sign = static_cast<uint32_t>(static_cast<int32_t>(difference) >> 31);
    return static_cast<int32_t>((difference ^ sign) - sign);
}

// Script job searches select the nearest eligible site by Manhattan tile distance, keeping the first candidate on ties.
// Mode 0 allows construction, 1 demolition and 2 either; other modes select nothing. Sites must have room for another builder.
// Assignment clears the manual flag, increments the job's builder count and reports 0 for construction or 1 for demolition in outIsBuild.
// Return the building id, or -1 without changing outIsBuild if no site qualifies or the builder is unregistered. Missing buildings are skipped.
template<class Env>
int AssignBuilderToNearestJob(Manager& manager, int builderId, const float* position, int mode, uint32_t* outIsBuild, Env& env)
{
    int from[2];
    env.WorldToTile(position, from);
    int best = 999999;
    Job* nearest = nullptr;
    for (const auto& owned : manager.jobs) {
        Job* job = owned.get();
        int tile[2];
        float limit;
        if (!env.BuildingTile(job->m_buildingId, tile) || !env.MaxBuilders(job->m_buildingId, &limit)) continue;
        const int distance = DistanceAbs(tile[1], from[1]) + DistanceAbs(tile[0], from[0]);
        const bool kindFits = (job->m_underConstruction && mode == 0) || (job->m_underDemolition && mode == 1) || mode == 2;
        if (JobHasRoom(job->m_builderCount, limit) && kindFits && distance < best) {
            best = distance;
            nearest = job;
        }
    }
    if (!nearest) return -1;
    Builder* entry = FindBuilder(manager, builderId);
    if (!entry) return -1;
    entry->m_buildingId = nearest->m_buildingId;
    entry->m_manuallyAssigned = 0;
    ++nearest->m_builderCount;
    *outIsBuild = nearest->m_underConstruction == 0;
    return nearest->m_buildingId;
}

template<class Env> int AssignBuilderToJob(Manager& manager, int builderId, int jobId, Env& env)
{
    Job* job = FindJob(manager, jobId);
    if (!job || job->m_finished) return -1;
    float limit;
    if (!env.MaxBuilders(job->m_buildingId, &limit) || !JobHasRoom(job->m_builderCount, limit)) return -1;
    Builder* entry = FindBuilder(manager, builderId);
    if (!entry) return -1;
    entry->m_buildingId = job->m_buildingId;
    entry->m_manuallyAssigned = 1;
    ++job->m_builderCount;
    return job->m_underConstruction == 0;
}

// A builder leaving a job releases its assignment and work-point claim but stays registered for later allocation.
// Reject an invalid table count before looking up the builder or changing assignments.
template<class Env> bool FreeBuilderOfJob(Manager& manager, int builderId, Env& env)
{
    const int count = manager.m_builderCount;
    if (count < 0 || count > kMaxBuilders) return false;
    ReleaseBuilder(manager, count, builderId, env);
    return true;
}

// Clear matching table assignments without notifying the job. Reallocation and script commands use this when the job's builder
// count and work-point claims must remain intact. Reject an invalid table count before changing entries.
inline bool ClearBuilderJobEntries(Manager& manager, int builderId)
{
    const int count = manager.m_builderCount;
    if (count < 0 || count > kMaxBuilders) return false;
    for (int i = 0; i < count; ++i) {
        if (manager.m_builders[i].m_builderId != builderId) continue;
        manager.m_builders[i].m_buildingId = -1;
        manager.m_builders[i].m_manuallyAssigned = 0;
    }
    return true;
}

struct BuilderFacts {
    bool homeExists = false;
    bool onShift = false; // Staff::IsOnShift (asked only when the home exists)
    bool homeOpen = false; // building_api::IsOpen of the home
    int inHut = 0;
    int state = 0;
};

constexpr int kBuilderStateDirected = 0x83;

template<class Env> bool ReallocateBuilders(Manager& manager, Env& env)
{
    const int entries = manager.m_builderCount;
    if (entries < 0 || entries > kMaxBuilders) return false;
    auto& builders = manager.m_builders;

    uint32_t usable = 0;
    bool homeMissing = false;
    for (int i = 0; i < entries; ++i) {
        BuilderFacts facts;
        if (!env.Builder(builders[i].m_builderId, &facts)) return false;
        if (!facts.homeExists) {
            homeMissing = true;
            continue;
        }
        builders[i].m_status = 1;
        if (facts.onShift && facts.homeOpen && facts.inHut == 0 && builders[i].m_manuallyAssigned == 0 &&
                facts.state != kBuilderStateDirected) {
            ++usable;
            builders[i].m_status = 0;
        }
    }
    if (homeMissing) return true;

    const uint32_t jobCount = static_cast<uint32_t>(manager.m_constructionCount);
    if (jobCount == 0) {
        for (int i = 0; i < entries; ++i) builders[i].m_buildingId = -1;
        return true;
    }
    uint32_t share = usable / jobCount;
    if (share == 0) share = 1;
    const uint32_t remainder = usable % jobCount;
    int32_t spare = 0;
    for (const auto& owned : manager.jobs) {
        const Job* job = owned.get();
        const uint32_t assigned = static_cast<uint32_t>(job->m_builderCount);
        if (!job->m_finished && assigned < share) spare += static_cast<int32_t>(share - assigned);
    }
    if (static_cast<int32_t>(remainder) > 1) ++spare;

    for (const auto& owned : manager.jobs) {
        Job* job = owned.get();
        if (job->m_finished || static_cast<uint32_t>(job->m_builderCount) <= share) continue;
        uint32_t surplus = static_cast<uint32_t>(job->m_builderCount) - share;
        for (int i = 0; i < entries && surplus != 0 && spare != 0; ++i) {
            const Builder& entry = builders[i];
            if (entry.m_buildingId != job->m_buildingId || entry.m_status != 0 || entry.m_manuallyAssigned != 0) continue;
            const int id = entry.m_builderId;
            int token = 0;
            const bool haveObject = env.BuilderToken(id, &token);
            for (int j = 0; j < entries; ++j) {
                if (builders[j].m_builderId != id) continue;
                if (Job* held = FindJob(manager, builders[j].m_buildingId)) {
                    --held->m_builderCount;
                    if (haveObject) {
                        for (int& owner : held->m_workPointOwners) {
                            if (owner != token) continue;
                            owner = -1;
                            break;
                        }
                    }
                }
                env.FreeBuilder(id);
            }
            --surplus;
            --spare;
        }
    }

    auto assign = [&](Builder& entry, Job& job) {
        entry.m_buildingId = job.m_buildingId;
        ++job.m_builderCount;
        env.Post(entry.m_builderId, job.m_underConstruction ? messages::MT_CONSTRUCT : messages::MT_DEMOLISH, entry.m_buildingId);
    };

    for (const auto& owned : manager.jobs) {
        Job* job = owned.get();
        if (job->m_finished || share <= static_cast<uint32_t>(job->m_builderCount)) continue;
        uint32_t need = share - static_cast<uint32_t>(job->m_builderCount);
        for (int i = 0; i < entries && need != 0; ++i) {
            Builder& entry = builders[i];
            if (entry.m_buildingId != -1 || entry.m_status != 0 || entry.m_manuallyAssigned != 0) continue;
            float limit;
            if (!env.MaxBuilders(job->m_buildingId, &limit)) return false;
            if (!JobHasRoom(job->m_builderCount, limit) || !(job->m_underConstruction || job->m_underDemolition)) continue;
            --need;
            assign(entry, *job);
        }
    }

    spare = 0;
    for (int i = 0; i < entries; ++i)
        if (builders[i].m_status == 0 && builders[i].m_buildingId == -1) ++spare;
    const int32_t extra = static_cast<int32_t>(std::ceil(static_cast<double>(spare) / static_cast<double>(static_cast<int32_t>(jobCount))));
    if (extra == 0) return true;

    for (const auto& owned : manager.jobs) {
        Job* job = owned.get();
        if (job->m_finished || static_cast<uint32_t>(job->m_builderCount) >= static_cast<uint32_t>(extra) + share) continue;
        uint32_t need = share - static_cast<uint32_t>(job->m_builderCount);
        if (spare != 0) need += static_cast<uint32_t>(extra);
        float limit;
        if (!env.MaxBuilders(job->m_buildingId, &limit)) return false;
        if (!JobHasRoom(job->m_builderCount, limit)) continue;
        for (int i = 0; i < entries && need != 0; ++i) {
            Builder& entry = builders[i];
            if (entry.m_buildingId != -1 || entry.m_status != 0) continue;
            --need;
            --spare;
            assign(entry, *job);
        }
    }
    return true;
}

struct CostFacts {
    bool hasDefinition = false;
    uint32_t buildTime = 0;
    uint32_t demolishTime = 0;
    uint32_t buildOn = 0;
};

// The work one call takes off a job: the frame time times the 0.01f constant.
constexpr float kCostPointStep = 0.01f;
constexpr double kBuildersLeftDelay = 11.0; // MT_BUILDERS_LEFT_BASEPLATE of a demolition in water, in game seconds
// The data of MT_BUILDERS_LEFT_BASEPLATE: which kind of job ended, the numbers of MT_CONSTRUCT and MT_DEMOLISH (the same pair construction::ReallocateBuilders sends).
constexpr int kCostPointBuilt = messages::MT_CONSTRUCT;
constexpr int kCostPointDemolished = messages::MT_DEMOLISH;

template<class Env> bool RemoveCostPoint(Manager& manager, int buildingId, Env& env)
{
    CostFacts facts;
    if (!env.Building(buildingId, &facts)) return true;
    Job* job = FindJob(manager, buildingId);
    if (!job || !job->m_started) return true;
    if (!facts.hasDefinition) return false;

    const bool construction = job->m_underConstruction != 0;
    float& remaining = construction ? job->m_constructionRemaining : job->m_demolitionRemaining;
    float& progress = construction ? job->m_constructionProgress : job->m_demolitionProgress;
    const double total = static_cast<double>(construction ? facts.buildTime : facts.demolishTime);
    const double left = static_cast<double>(remaining) - static_cast<double>(env.FrameTime()) * static_cast<double>(kCostPointStep);
    remaining = static_cast<float>(left);
    progress = static_cast<float>(1.0 - left / total);
    if (job->m_finished || remaining > 0.0f) return true;
    job->m_finished = 1;

    for (int i = 0; static_cast<uint32_t>(i) < static_cast<uint32_t>(manager.m_builderCount); ++i) {
        if (i >= kMaxBuilders) return false;
        const Builder& entry = manager.m_builders[i];
        if (entry.m_buildingId == buildingId) env.Post(entry.m_builderId, entry.m_builderId, messages::MT_CONSTRUCTION_FINISHED, 0);
    }

    --manager.m_constructionCount;
    if (job->m_underConstruction) {
        env.Post(buildingId, buildingId, messages::MT_BUILDERS_LEFT_BASEPLATE, kCostPointBuilt);
        return true;
    }
    const float delay = (facts.buildOn & 0xc) != 0 ? static_cast<float>(kBuildersLeftDelay) : 0.0f;
    env.PostDelayed(buildingId, buildingId, messages::MT_BUILDERS_LEFT_BASEPLATE, kCostPointDemolished, delay);
    return true;
}

} // namespace obl::construction
