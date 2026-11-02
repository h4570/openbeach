#pragma once

#include "construction_jobs.h"

#include <cstdint>

namespace obl::construction {

// What the starts read from the building object and its definition (BuildingDef via Building::m_definition).
struct StartFacts {
    bool hasDefinition = false;
    int buildTime = 0;
    int demolishTime = 0;
    bool hasFootprint = false;
    bool jetty = false; // the definition's behavior is Jetty (every key containing "JETTY")
};

// The message a start sends: MT_STATECHANGE (3) with state 0, building to itself (StartBuild when the world starts up or the building is a jetty).
constexpr int kStartStateChange = messages::MT_STATECHANGE;

// A new job at the end of the list, made from the building's definition (construction::ConstructJob reads the same fields).
inline Job& AddJob(Manager& manager, int buildingId, const StartFacts& facts)
{
    return AppendJob(manager, buildingId, {facts.buildTime, facts.demolishTime, facts.hasFootprint});
}

template<class Env> bool StartBuild(Manager& manager, int buildingId, Env& env)
{
    StartFacts facts;
    if (!env.Building(buildingId, &facts) || !facts.hasDefinition) return false;
    if (!env.Startup()) ++manager.m_constructionCount;
    Job* job = &AddJob(manager, buildingId, facts);
    if (Job* first = FindJob(manager, buildingId)) first->m_started = 1;
    env.ApplyBuildingToGrid(buildingId);
    if (facts.hasFootprint) env.CollectWorkPoints(buildingId);
    job->m_underConstruction = 1;
    if (!env.Startup() && !facts.jetty) {
        env.Reallocate();
        return true;
    }
    env.Post(buildingId, buildingId, kStartStateChange, 0);
    return true;
}

template<class Env> bool StartDemolish(Manager& manager, int buildingId, Env& env)
{
    StartFacts facts;
    if (!env.Building(buildingId, &facts) || !facts.hasDefinition) return false;
    Job* job = &AddJob(manager, buildingId, facts);
    ++manager.m_constructionCount;
    if (facts.hasFootprint) env.CollectWorkPoints(buildingId);
    job->m_underDemolition = 1;
    env.PostToHumans(buildingId, messages::MT_BUILDING_BEING_REMOVED, 0);
    env.RemoveRepairJob(buildingId);
    env.RemoveCleaningJob(buildingId);
    if (facts.hasFootprint) {
        int tile[2];
        if (!env.TilePosition(buildingId, tile)) return false;
        env.ApplyFootprint(static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1]), 0, 3, buildingId);
    }
    int extents[4];
    if (!env.Extents(buildingId, extents)) return false;
    env.QueueBlockRebuild(extents[0], extents[1], static_cast<int>(static_cast<uint32_t>(extents[2]) - static_cast<uint32_t>(extents[0])),
            static_cast<int>(static_cast<uint32_t>(extents[3]) - static_cast<uint32_t>(extents[1])));
    env.Reallocate();
    return true;
}

} // namespace obl::construction
