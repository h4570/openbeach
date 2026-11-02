#pragma once

#include "game/construction/construction_state.h"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace obl::construction {

using construction::Job;
using construction::Manager;

// construction::GetBuildProgress/GetDemolishProgress clamp to 0..1 with plain compares: a NaN counts as "below 0", -0.0 passes unchanged.
inline float ClampProgress(float value)
{
    // Optimized floating min/max can replace -0 with +0. Classify IEEE bits to keep the signed-zero contract.
    const auto bits = std::bit_cast<uint32_t>(value);
    const auto magnitude = bits & 0x7fffffffu;
    if (magnitude > 0x7f800000u || ((bits & 0x80000000u) && magnitude != 0)) return 0.0f;
    return magnitude <= 0x3f800000u ? value : 1.0f;
}

// construction::FindJob: first job in list order whose building id matches, null when none.
inline Job* FindJob(const Manager& manager, int buildingId)
{
    for (const auto& job : manager.jobs)
        if (job->m_buildingId == buildingId) return job.get();
    return nullptr;
}

// construction::CountJobs: mode 0 counts construction jobs, 1 demolition jobs, 2 every job (also a finished one
// that is neither); any other mode counts nothing.
inline int CountJobs(const Manager& manager, int mode)
{
    int count = 0;
    for (const auto& job : manager.jobs) {
        if ((job->m_underConstruction && mode == 0) || (job->m_underDemolition && mode == 1) || mode == 2) ++count;
    }
    return count;
}

// construction::CountUsableBuilders: registered builders that are on shift at an open home building.
template<class Env> int CountUsableBuilders(const Manager& manager, Env& env)
{
    int count = 0;
    const int builders = std::min(manager.m_builderCount, kMaxBuilders);
    for (int i = 0; i < builders; ++i)
        if (env.BuilderCanWork(manager.m_builders[i].m_builderId)) ++count;
    return count;
}

// construction::GetBuildProgress / GetDemolishProgress: no job counts as finished (1.0).
inline float BuildProgress(const Manager& manager, int buildingId)
{
    const Job* job = FindJob(manager, buildingId);
    return job ? ClampProgress(job->m_constructionProgress) : 1.0f;
}
inline float DemolishProgress(const Manager& manager, int buildingId)
{
    const Job* job = FindJob(manager, buildingId);
    return job ? ClampProgress(job->m_demolitionProgress) : 1.0f;
}

// construction::IsBuilding / IsDemolishing: despite the names the results are floats (0.0 or 1.0 from an unsigned 64-bit read of
// the byte), and both ask the ObjectManager first: a building id without an object answers 0 even when a job exists.
template<class Env> float IsBuilding(const Manager& manager, Env& env, int buildingId)
{
    if (!env.ObjectExists(buildingId)) return 0.0f;
    const Job* job = FindJob(manager, buildingId);
    return job ? static_cast<float>(job->m_underConstruction) : 0.0f;
}
template<class Env> float IsDemolishing(const Manager& manager, Env& env, int buildingId)
{
    if (!env.ObjectExists(buildingId)) return 0.0f;
    const Job* job = FindJob(manager, buildingId);
    return job ? static_cast<float>(job->m_underDemolition) : 0.0f;
}

// construction::IsStarted: the raw byte, not normalised to 0/1 (callers test the byte).
inline uint8_t IsStarted(const Manager& manager, int buildingId)
{
    const Job* job = FindJob(manager, buildingId);
    return job ? job->m_started : 0;
}

// construction::GetBuilderLocation: the builder's BuilderLocation state, -1 for an unregistered builder.
inline int GetBuilderLocation(const Manager& manager, int builderId)
{
    const int builders = std::min(manager.m_builderCount, kMaxBuilders);
    for (int i = 0; i < builders; ++i)
        if (manager.m_builders[i].m_builderId == builderId) return static_cast<int>(manager.m_builders[i].m_location);
    return -1;
}

// construction::GetWorkDirection: direction of the builder's work point turned by the building's rotation (2 steps of
// eight per rotation step). The slot is the one whose token is `builderId`, slot 0 when the builder holds none; no job or no work points = 0.
template<class Env> int GetWorkDirection(const Manager& manager, Env& env, int buildingId, int builderId)
{
    const Job* job = FindJob(manager, buildingId);
    if (!job || !job->hasWorkPoints) return 0;
    const int rotation = env.BuildingDirection(buildingId);
    int slot = 0;
    for (int i = 0; i < 10; ++i) {
        if (job->m_workPointOwners[i] == builderId) {
            slot = i;
            break;
        }
    }
    const int direction = job->workPoints[slot].direction - 2 * rotation;
    return direction < 0 ? direction + 8 : direction;
}

} // namespace obl::construction
