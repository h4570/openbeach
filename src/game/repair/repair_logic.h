#pragma once

#include "game/repair/repair_state.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace obl::repair {

// Job types (Job::type / ActiveJob::type): AddJob priority 0 = inspection, 2 = breakdown, 1 = scenery repair
// (AddUrgentJob picks 1 for non-buildings), 3 = watering (never queued, only claimed).
constexpr uint32_t kTypeInspection = 0, kTypeScenery = 1, kTypeBreakdown = 2, kTypeWatering = 3;
// Prop::m_type of a building (AddUrgentJob decides the list by it).
constexpr uint8_t kObjectTypeScenery = 1, kObjectTypeBuilding = 2;
// A request younger than this many World::m_gameTime units is not handed out yet (ScanJobs).
constexpr double kMinWaitingTime = 5.0;
// FindPlantToWater with radius 0 (FindWateringJob / LookWatering): a plant counts when it is farther than this; one draw in three
// uses the larger distance.
constexpr float kFarPlantNear = 120.0f, kFarPlantFar = 150.0f;
constexpr int kFindPlantRadius = 200; // FindJob's kind 3 asks for plants within this many units

struct PlantInfo {
    int id = 0;
    float pos[3] = {0, 0, 0};
    bool waterable = false; // SceneryDef::m_water
    bool inactive = false; // Entity::m_waitingForDeath: dying plants are skipped
};

inline double DistanceSquared(const float* object, const float* point)
{
    double dx = static_cast<double>(object[0]) - static_cast<double>(point[0]);
    double dy = static_cast<double>(object[1]) - static_cast<double>(point[1]);
    double dz = static_cast<double>(object[2]) - static_cast<double>(point[2]);
    return (dz * dz + dy * dy) + dx * dx;
}

// The first record of `id` in a list, or end().
template<class List> auto Find(List& list, int id)
{
    return std::find_if(list.begin(), list.end(), [id](const auto& job) { return job.id == id; });
}

// Drops the record `job` points at (a scan winner) from its list.
inline void Erase(JobList& list, const Job* job)
{
    list.erase(std::find_if(list.begin(), list.end(), [job](const Job& j) { return &j == job; }));
}

template<class Env> void ScanJobs(Manager& manager, Env& env, Scan& result, const float* point, int kind)
{
    auto waited = [&](const Job& job) {
        return static_cast<double>(env.GameTime()) - static_cast<double>(job.timeCalled) > kMinWaitingTime;
    };
    // Shared candidate step: measure, store the float in the record and take the slot if empty or nearer.
    auto consider = [&](Job& job, uint8_t& found, Job*& best, JobList*& bestList, JobList& list) {
        float position[3];
        env.Position(job.id, position);
        double d = DistanceSquared(position, point);
        job.distance = static_cast<float>(d);
        if (found == 0 || d < static_cast<double>(best->distance)) {
            found = 1;
            best = &job;
            bestList = &list;
        }
    };

    for (Job& job : manager.buildingJobs) {
        if (job.urgent != 0) {
            if (waited(job) && env.ObjectExists(job.id) && !env.IsMechanicClaimed(job.id))
                consider(job, result.urgentFound, result.urgent, result.urgentList, manager.buildingJobs);
        } else if (job.type == static_cast<int>(kTypeBreakdown) && kind == static_cast<int>(kTypeBreakdown)) {
            if (env.ObjectExists(job.id) && !env.IsCleanerClaimed(job.id) && waited(job))
                consider(job, result.kind1Found, result.kind1, result.kind1List, manager.buildingJobs);
        } else if (job.type == static_cast<int>(kTypeInspection) && kind == static_cast<int>(kTypeInspection)) {
            if (env.ObjectExists(job.id) && !env.IsCleanerClaimed(job.id) && waited(job))
                consider(job, result.kind0Found, result.kind0, result.kind0List, manager.buildingJobs);
        }
    }
    if (result.urgentFound != 0) return;
    for (Job& job : manager.sceneryJobs) {
        if (job.urgent != 0) {
            if (waited(job) && env.ObjectExists(job.id) && !env.IsCleanerClaimed(job.id))
                consider(job, result.urgentFound, result.urgent, result.urgentList, manager.sceneryJobs);
        } else if (job.type == static_cast<int>(kTypeScenery) && kind == static_cast<int>(kTypeScenery)) {
            if (env.ObjectExists(job.id) && !env.IsCleanerClaimed(job.id) && waited(job))
                consider(job, result.kind1Found, result.kind1, result.kind1List, manager.sceneryJobs);
        }
    }
}

// ClaimJob: appends an active record unless the object is already active. The active counter grows here and is never decremented
// anywhere in the manager.
template<class Env> void ClaimJob(Manager& manager, Env&, int id, uint8_t urgent, uint32_t type)
{
    if (Find(manager.activeJobs, id) != manager.activeJobs.end()) return;
    ++manager.activeCount;
    manager.activeJobs.push_back({id, 0, urgent, static_cast<int32_t>(type)});
}

// ReleaseJob: drops the active record of `id`, if any.
template<class Env> void ReleaseJob(Manager& manager, Env&, int id)
{
    if (auto active = Find(manager.activeJobs, id); active != manager.activeJobs.end()) manager.activeJobs.erase(active);
}

// StartedJob: the mechanic has begun (repairing = 1).
template<class Env> void StartedJob(Manager& manager, Env&, int id)
{
    if (auto active = Find(manager.activeJobs, id); active != manager.activeJobs.end()) active->repairing = 1;
}

template<class Env> int GetJobStatus(const Manager& manager, Env& env, int id)
{
    if (env.MechanicCount() == 0) return 0;
    const JobList& list = (env.ObjectExists(id) && env.ObjectClass(id) == kObjectTypeBuilding) ? manager.buildingJobs : manager.sceneryJobs;
    if (auto active = Find(manager.activeJobs, id); active != manager.activeJobs.end()) return (active->repairing != 0) + 5;
    auto pending = Find(list, id);
    if (pending == list.end()) return 2;
    return (pending->type != static_cast<int>(kTypeBreakdown)) + 3;
}

// GetJobType: the type a mechanic holds the object under (active list only), -1 when it holds none.
template<class Env> int GetJobType(const Manager& manager, Env&, int id)
{
    auto active = Find(manager.activeJobs, id);
    return active == manager.activeJobs.end() ? -1 : active->type;
}

// AddJob: registers a request. Objects the world does not hold are ignored. A job the mechanics already hold only has its type raised; a pending one
// counts the repeated request (and an inspection is promoted to a breakdown); otherwise a new record is queued at the back of the
// list for the type (2 and 0: buildings, other: scenery).
template<class Env> void AddJob(Manager& manager, Env& env, int id, uint32_t priority)
{
    if (!env.ObjectExists(id)) return;
    JobList& list = (priority == kTypeBreakdown || priority == kTypeInspection) ? manager.buildingJobs : manager.sceneryJobs;
    if (auto active = Find(manager.activeJobs, id); active != manager.activeJobs.end()) {
        if (static_cast<uint32_t>(active->type) < priority) active->type = static_cast<int32_t>(priority);
        return;
    }
    if (auto pending = Find(list, id); pending != list.end()) {
        if (pending->type == 0 && priority == kTypeBreakdown) pending->type = static_cast<int32_t>(kTypeBreakdown);
        ++pending->calls;
        return;
    }
    list.push_back({id, 0, 0.0f, env.GameTime(), 0, static_cast<int32_t>(priority)});
}

// AddUrgentJob: like AddJob but marks the request urgent; the list is chosen by the object's class (buildings: type 2, everything
// else: type 1). An active job just becomes urgent (and its type may rise).
template<class Env> void AddUrgentJob(Manager& manager, Env& env, int id)
{
    if (!env.ObjectExists(id)) return;
    bool building = env.ObjectClass(id) == kObjectTypeBuilding;
    uint32_t type = building ? kTypeBreakdown : kTypeScenery;
    JobList& list = building ? manager.buildingJobs : manager.sceneryJobs;
    if (auto active = Find(manager.activeJobs, id); active != manager.activeJobs.end()) {
        active->urgent = 1;
        if (static_cast<uint32_t>(active->type) < type) active->type = static_cast<int32_t>(type);
        return;
    }
    if (auto pending = Find(list, id); pending != list.end()) {
        if (pending->urgent == 0) {
            pending->urgent = 1;
            pending->type = static_cast<int32_t>(type);
        }
        ++pending->calls;
        return;
    }
    list.push_back({id, 0, 0.0f, env.GameTime(), 1, static_cast<int32_t>(type)});
}

template<class Env> void ReturnJob(Manager& manager, Env& env, int id, uint32_t type)
{
    auto active = Find(manager.activeJobs, id);
    bool urgent = active != manager.activeJobs.end() && active->urgent != 0;
    ReleaseJob(manager, env, id);
    if (type != kTypeWatering) {
        if (urgent) {
            AddUrgentJob(manager, env, id);
            env.SetMechanicClaimed(id, false);
            return;
        }
        AddJob(manager, env, id, type);
    }
    env.SetMechanicClaimed(id, false);
}

// Hands the winner of a scan over to a mechanic: claim, report the type, mark the object, drop the pending record.
template<class Env> int TakeJob(Manager& manager, Env& env, JobList& list, Job* job, uint8_t urgent, uint32_t* typeOut)
{
    int id = job->id;
    ClaimJob(manager, env, id, urgent, static_cast<uint32_t>(job->type));
    *typeOut = static_cast<uint32_t>(job->type);
    env.SetMechanicClaimed(id, true);
    Erase(list, job);
    return id;
}

// FindPlantToWater: `count` times it draws a random plant (rand() % count, repeats allowed). A plant qualifies when it is waterable,
// not dying, not already active and: for radius > 0 closer than the radius; for radius 0 farther than 120 (or 150 on one draw in
// three - a second rand() per tested plant). Returns the object id or -1. The three coordinate differences are rounded to float
// before they are squared.
template<class Env> int FindPlantToWater(Manager& manager, Env& env, const float* point, uint32_t radius)
{
    size_t count = env.PlantCount();
    for (size_t attempt = 0; attempt < count; ++attempt) {
        size_t index = static_cast<uint32_t>(env.Rand()) % static_cast<uint32_t>(count);
        PlantInfo plant = env.PlantAt(index);
        if (!plant.waterable || plant.inactive) continue;
        float dx = point[0] - plant.pos[0];
        float dy = point[1] - plant.pos[1];
        float dz = point[2] - plant.pos[2];
        if (Find(manager.activeJobs, plant.id) != manager.activeJobs.end()) continue;
        double distance = std::sqrt((static_cast<double>(dy) * dy + static_cast<double>(dz) * dz) + static_cast<double>(dx) * dx);
        if (radius == 0) {
            int draw = env.Rand();
            if (draw % 3 == 0) {
                if (distance > kFarPlantFar) return plant.id;
            } else if (distance > kFarPlantNear) {
                return plant.id;
            }
        } else if (distance < static_cast<double>(radius)) {
            return plant.id;
        }
    }
    return -1;
}

// ClaimObject (reached from Mechanic::DirectToObject): the player sends a mechanic to a chosen object. Only a free building or
// scenery object qualifies (neither cleaner- nor mechanic-claimed). A building is claimed as an urgent type-2 job, a waterable
// scenery object as type 3, any other scenery as type 1; a matching pending request is dropped from its queue and its type reported
// (otherwise the job's own type). Returns the id or -1.
template<class Env> int ClaimObject(Manager& manager, Env& env, int id, uint32_t* typeOut)
{
    if (!env.ObjectExists(id)) return -1;
    uint8_t objectClass = env.ObjectClass(id);
    if (objectClass != kObjectTypeBuilding && objectClass != kObjectTypeScenery) return -1;
    if (env.IsCleanerClaimed(id) || env.IsMechanicClaimed(id)) return -1;
    JobList* list = &manager.buildingJobs;
    uint32_t type = kTypeBreakdown;
    if (objectClass == kObjectTypeScenery) {
        if (env.Waterable(id)) {
            ClaimJob(manager, env, id, 1, kTypeWatering);
            *typeOut = kTypeWatering;
            env.SetDirected(id);
            env.SetMechanicClaimed(id, true);
            return id;
        }
        list = &manager.sceneryJobs;
        type = kTypeScenery;
    }
    auto pending = Find(*list, id);
    ClaimJob(manager, env, id, 1, type);
    if (pending == list->end()) {
        *typeOut = type;
    } else {
        *typeOut = static_cast<uint32_t>(pending->type);
        list->erase(pending);
    }
    env.SetDirected(id);
    env.SetMechanicClaimed(id, true);
    return id;
}

// FindWateringJob: a gardener far from its last plant picks another one and claims it as a normal (not urgent) watering job.
template<class Env> int FindWateringJob(Manager& manager, Env& env, const float* point)
{
    int id = FindPlantToWater(manager, env, point, 0);
    if (id == -1) return -1;
    ClaimJob(manager, env, id, 0, kTypeWatering);
    env.SetMechanicClaimed(id, true);
    return id;
}

// FindJob: kind 3 is watering (a plant within 200, claimed as an urgent type-3 job); kinds 0..2 scan the queues: an urgent job wins,
// otherwise the kind-1 slot (breakdown or scenery), otherwise kind 0. The winner is claimed and removed from the queue. Returns the
// object id or -1; *type is only written on success.
template<class Env> int FindJob(Manager& manager, Env& env, const float* point, int kind, uint32_t* typeOut)
{
    if (kind == static_cast<int>(kTypeWatering)) {
        int id = FindPlantToWater(manager, env, point, kFindPlantRadius);
        if (id == -1) return -1;
        ClaimJob(manager, env, id, 1, kTypeWatering);
        *typeOut = kTypeWatering;
        env.SetMechanicClaimed(id, true);
        return id;
    }
    Scan scan;
    ScanJobs(manager, env, scan, point, kind);
    if (scan.urgentFound != 0) return TakeJob(manager, env, *scan.urgentList, scan.urgent, 1, typeOut);
    if (scan.kind1Found != 0) return TakeJob(manager, env, *scan.kind1List, scan.kind1, 0, typeOut);
    if (scan.kind0Found != 0) return TakeJob(manager, env, *scan.kind0List, scan.kind0, 0, typeOut);
    return -1;
}

template<class Env> int LookForBetterJob(Manager& manager, Env& env, const float* point, int currentId, uint32_t* typeOut)
{
    auto current = Find(manager.activeJobs, currentId);
    if (current == manager.activeJobs.end()) return -1;
    if (current->type == static_cast<int>(kTypeWatering) && current->urgent != 0) return -1;
    uint32_t heldType = static_cast<uint32_t>(current->type);
    bool heldUrgent = current->urgent != 0;

    float position[3];
    env.Position(currentId, position);
    float currentDistance = static_cast<float>(DistanceSquared(position, point));

    Scan scan;
    ScanJobs(manager, env, scan, point, static_cast<int>(heldType));
    // ReturnJob only appends to the queues or changes records in place, so the scan's pointers stay valid.
    auto swap_to = [&](JobList& list, Job* job, uint8_t urgent) {
        ReturnJob(manager, env, currentId, heldType);
        return TakeJob(manager, env, list, job, urgent, typeOut);
    };
    if (scan.urgentFound != 0) {
        if (!heldUrgent) return swap_to(*scan.urgentList, scan.urgent, 1);
        env.Position(scan.urgent->id, position);
        if (DistanceSquared(position, point) < static_cast<double>(currentDistance)) return swap_to(*scan.urgentList, scan.urgent, 1);
    }
    if ((scan.kind1Found != 0 || scan.kind0Found != 0) && !heldUrgent) {
        Job* job = scan.kind1Found != 0 ? scan.kind1 : scan.kind0;
        JobList& list = scan.kind1Found != 0 ? *scan.kind1List : *scan.kind0List;
        uint32_t candidateType = static_cast<uint32_t>(job->type);
        if (candidateType >= heldType && job->id != currentId) {
            env.Position(job->id, position);
            if (DistanceSquared(position, point) < static_cast<double>(currentDistance) || candidateType > heldType)
                return swap_to(list, job, 0);
        }
    }
    return -1;
}

} // namespace obl::repair
