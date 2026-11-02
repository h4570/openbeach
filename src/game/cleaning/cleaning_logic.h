#pragma once

#include "core/ascii_case.h"
#include "game/tables/building_behavior.h"
#include "game/cleaning/cleaning_state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace obl::cleaning {

// What a cleaning request is about: the type FindUrgentJob/FindBestScoredJob/ClaimObject report. Litter and bins are scenery named so; a
// toilet is a building with the toilet behavior (the catalog's LOO), so it is decided by the behavior, not by the name.
constexpr int kTypeLitter = 0, kTypeBin = 1, kTypeToilet = 2, kTypeNone = -1;
constexpr const char* kLitterName = "Litter";
constexpr const char* kBinName = "Bin";
constexpr uint8_t kObjectTypeScenery = 1, kObjectTypeBuilding = 2;
// FindBestScoredJob ranks by tile distance minus these bonuses (a toilet is worth walking farthest for) against a worst score of 99999.
constexpr int kLitterBonus = 0x32, kBinBonus = 0x46, kToiletBonus = 0x64, kWorstScore = 99999;

inline bool NameIs(const char* name, const char* expected)
{
    return ascii::EqualsNoCase(std::string_view(name, strnlen(name, 32)), expected);
}

inline int TypeOfName(const char* name)
{
    if (NameIs(name, kLitterName)) return kTypeLitter;
    if (NameIs(name, kBinName)) return kTypeBin;
    return kTypeNone;
}

// The kind of an object for a request: a toilet by its behavior, the other two by name.
template<class Env> int KindOf(Env& env, int id)
{
    if (env.ObjectBehavior(id) == BuildingBehavior::Toilet) return kTypeToilet;
    return TypeOfName(env.ObjectName(id));
}

template<class List> auto FindById(List& list, int id)
{
    return std::find_if(list.begin(), list.end(), [id](const auto& record) { return record.id == id; });
}

template<class Env> void QueueJob(Manager& manager, Env& env, int id, uint8_t urgent)
{
    Job job;
    job.calls = 0;
    job.id = id;
    job.timeCalled = env.GameTime();
    job.urgent = urgent;
    manager.jobs.push_back(job);
}

template<class Env> void AddJob(Manager& manager, Env& env, int id)
{
    if (auto pending = FindById(manager.jobs, id); pending != manager.jobs.end()) {
        ++pending->calls;
        return;
    }
    QueueJob(manager, env, id, 0);
}

template<class Env> void AddUrgentJob(Manager& manager, Env& env, int id)
{
    if (auto pending = FindById(manager.jobs, id); pending != manager.jobs.end()) {
        if (pending->urgent != 0) {
            ++pending->calls;
            return;
        }
        manager.jobs.erase(pending);
        QueueJob(manager, env, id, 1);
        return;
    }
    if (auto active = FindById(manager.activeJobs, id); active != manager.activeJobs.end()) {
        active->urgent = 1;
        return;
    }
    QueueJob(manager, env, id, 1);
}

inline void ClaimJob(Manager& manager, int id, uint8_t urgent)
{
    if (FindById(manager.activeJobs, id) != manager.activeJobs.end()) return;
    ActiveJob job;
    job.id = id;
    job.cleaning = 0;
    job.urgent = urgent;
    manager.activeJobs.push_back(job);
    ++manager.activeCount;
}

inline void ReleaseJob(Manager& manager, int id)
{
    auto active = FindById(manager.activeJobs, id);
    if (active == manager.activeJobs.end()) return;
    manager.activeJobs.erase(active);
    --manager.activeCount;
}

inline void RemoveJob(Manager& manager, int id)
{
    if (auto pending = FindById(manager.jobs, id); pending != manager.jobs.end()) manager.jobs.erase(pending);
}

template<class Env> void StartedCleaning(Manager& manager, Env& env, int id)
{
    if (!env.ObjectExists(id)) return;
    if (auto active = FindById(manager.activeJobs, id); active != manager.activeJobs.end()) active->cleaning = 1;
}

template<class Env> void DidNotClean(Manager& manager, Env& env, int id)
{
    auto active = FindById(manager.activeJobs, id);
    bool urgent = active != manager.activeJobs.end() && active->urgent != 0;
    ReleaseJob(manager, id);
    if (env.ObjectExists(id)) env.SetCleanerClaimed(id, false);
    if (urgent)
        AddUrgentJob(manager, env, id);
    else
        AddJob(manager, env, id);
}

template<class Env> int GetJobStatus(const Manager& manager, Env& env, int id)
{
    if (env.CleanerCount() == 0) return 1;
    if (auto active = FindById(manager.activeJobs, id); active != manager.activeJobs.end()) return (active->cleaning != 0) + 4;
    return (FindById(manager.jobs, id) != manager.jobs.end()) + 2;
}

template<class Env> int ClaimObject(Manager& manager, Env& env, int id)
{
    if (!env.ObjectExists(id)) return -1;
    uint8_t objectClass = env.ObjectClass(id);
    if (objectClass != kObjectTypeBuilding && objectClass != kObjectTypeScenery) return -1;
    if (env.IsCleanerClaimed(id) || env.IsMechanicClaimed(id)) return -1;
    auto pending = FindById(manager.jobs, id);
    ClaimJob(manager, id, 1);
    if (pending != manager.jobs.end()) manager.jobs.erase(pending);
    env.SetCleaningDirected(id);
    env.SetCleanerClaimed(id, true);
    return KindOf(env, id);
}

// Hands the winner of a search over to a cleaner: claim, mark the object, free the pending record.
template<class Env> int TakeJob(Manager& manager, Env& env, JobList::iterator pending, uint8_t urgent)
{
    int id = pending->id;
    ClaimJob(manager, id, urgent);
    env.SetCleanerClaimed(id, true);
    manager.jobs.erase(pending);
    return id;
}

template<class Env> int FindUrgentJob(Manager& manager, Env& env, int* typeOut, bool bins, bool scenery, bool buildings)
{
    auto winner = manager.jobs.end();
    for (auto pending = manager.jobs.begin(); pending != manager.jobs.end(); ++pending) {
        float time = pending->timeCalled;
        if (time == 0.0f || time != time) // compare: equal and unordered both skip
            continue;
        if (pending->urgent == 0) continue;
        int id = pending->id;
        if (!env.ObjectExists(id) || env.IsCleanerClaimed(id)) continue;
        if (NameIs(env.ObjectName(id), kBinName)) {
            if (bins && !env.Vandalised(id) && !env.IsMechanicClaimed(id)) winner = pending;
        } else if (env.ObjectClass(id) == kObjectTypeBuilding) {
            if (buildings && !env.HasBuildJob(id) && !env.HasDemolishJob(id)) winner = pending;
        } else if (scenery) {
            winner = pending;
        }
    }
    if (winner == manager.jobs.end()) return -1;
    *typeOut = KindOf(env, winner->id);
    return TakeJob(manager, env, winner, 1);
}

template<class Env>
int FindBestScoredJob(Manager& manager, Env& env, const float* point, int* typeOut, bool bins, bool litter, bool toilets)
{
    if (manager.jobs.empty()) return -1;
    int target[2];
    env.WorldToTile(point, target);
    int best = kWorstScore;
    auto winner = manager.jobs.end();
    int score = 0, kind = kTypeNone;
    for (auto pending = manager.jobs.begin(); pending != manager.jobs.end();) {
        int id = pending->id;
        if (!env.ObjectExists(id)) {
            pending = manager.jobs.erase(pending);
            continue;
        }
        int tile[2];
        env.ObjectTile(id, tile);
        int distance = std::abs(tile[1] - target[1]) + std::abs(tile[0] - target[0]);
        const char* name = env.ObjectName(id);
        if (NameIs(name, kLitterName))
            score = distance - kLitterBonus, kind = kTypeLitter;
        else if (NameIs(name, kBinName))
            score = distance - kBinBonus, kind = kTypeBin;
        else if (env.ObjectBehavior(id) == BuildingBehavior::Toilet)
            score = distance - kToiletBonus, kind = kTypeToilet;
        if (score < best) {
            bool accepted = false;
            if (kind == kTypeLitter) {
                accepted = litter && !env.IsCleanerClaimed(id);
            } else if (kind == kTypeBin) {
                accepted = bins && !env.Vandalised(id) && (!env.IsCleanerClaimed(id) || !env.IsMechanicClaimed(id));
            } else if (kind == kTypeToilet) {
                accepted = toilets && !env.HasBuildJob(id) && !env.HasDemolishJob(id) && !env.IsCleanerClaimed(id) &&
                        !env.IsMechanicClaimed(id);
            }
            if (accepted) {
                winner = pending;
                best = score;
                *typeOut = kind;
            }
        }
        ++pending;
    }
    if (winner == manager.jobs.end()) return -1;
    return TakeJob(manager, env, winner, 0);
}

} // namespace obl::cleaning
