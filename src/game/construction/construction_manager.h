#pragma once

#include "game/construction/construction_state.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace obl::construction {

// Runtime adapters bind construction state to world objects, messages and the grid.
namespace runtime {

// The lifecycle: Create (new, World construct) and Destroy (World destruct), the save block (game/construction/construction_lifecycle.h).
construction::Manager* Create();
void Destroy(construction::Manager* manager);
bool LoadJson(construction::Manager* manager, const nlohmann::json& in, std::string& error);

// Test observation: the pending fences of the last job that had any after the latest Load. The game consumes pending fences within a
// few frames of a load, so tests cannot inspect them in the live job.
struct LoadedFences {
    int buildId = -1;
    int total = 0; // the job's fence count, records holds the first few
    std::array<construction::Hoarding, 4> records{};
};
const LoadedFences& LastLoadedFences();

// Group A: read-only queries (src/game/construction/construction_queries.h).
construction::Job* FindJob(construction::Manager* manager, int buildingId);
int CountJobs(construction::Manager* manager, int mode);
int CountUsableBuilders(construction::Manager* manager);
float BuildProgress(construction::Manager* manager, int buildingId);
float DemolishProgress(construction::Manager* manager, int buildingId);
float IsBuilding(construction::Manager* manager, int buildingId);
float IsDemolishing(construction::Manager* manager, int buildingId);
// The raw byte; a bool here would normalise it.
uint8_t IsStarted(construction::Manager* manager, int buildingId);
int GetBuilderLocation(construction::Manager* manager, int builderId);
int GetWorkDirection(construction::Manager* manager, int buildingId, int builderId);

// The build flow (src/game/construction/construction_job_start.h, construction_job_finish.h): StartBuild and StartDemolish are called on the
// building's state change, FinishBuild and FinishDemolish when the work is done, SpawnHoarding by TickHoarding.
void StartBuild(construction::Manager* manager, int buildingId);
void StartDemolish(construction::Manager* manager, int buildingId);
void FinishBuild(construction::Manager* manager, int buildingId);
void FinishDemolish(construction::Manager* manager, int buildingId);
void SpawnHoarding(construction::Manager* manager, int buildingId);

// Group H: the occupancy grid of a building (src/game/construction/construction_footprint.h).
void ClearFootprint(construction::Manager* manager, int buildingId);
void ApplyFootprint(construction::Manager* manager, uint32_t tileX, uint32_t tileY, int phase, int mode, int buildingId);
void ApplyBuildingToGrid(construction::Manager* manager, int buildingId);

// Group E: the builder assignment (src/game/construction/construction_jobs.h).
int AssignBuilderToNearestJob(construction::Manager* manager, int builderId, const float* position, int mode, uint32_t* outIsBuild);
int AssignBuilderToJob(construction::Manager* manager, int builderId, int jobId);

// Group D: the work points of a site (src/game/construction/construction_points.h). BuildEntryPoint is the builder script's
// entry point with the builder's fallback position; the claim changes the world's manager.
void CollectWorkPoints(construction::Manager* manager, int buildingId);
void BuildEntryPoint(int buildingId, int builderToken, const float fallback[3], float out[3]);

// Group B: the setters and the hoarding tick.
void SetStarted(construction::Manager* manager, int buildingId, uint8_t started);
void SetBuilderLocation(construction::Manager* manager, int builderId, int location);
void TickHoarding(construction::Manager* manager, int buildingId);

// Groups E and F: giving a job back (FreeBuilderOfJob tells the job, ...NoBuildCallCheck only clears the builder's entries), spreading
// the builders over the jobs (ReallocateBuilders) and working off a job (RemoveCostPoint): the construction functions, called
// by the builder script, the staff code and the other construction facades.
void FreeBuilderOfJob(construction::Manager* manager, int builderId);
void ClearBuilderJobEntries(construction::Manager* manager, int builderId);
void ReallocateBuilders(construction::Manager* manager);
void RemoveCostPoint(construction::Manager* manager, int buildingId);

// Groups C1 and C2: the jobs and the builder table.
void DeleteJob(construction::Manager* manager, int buildingId);
void AddBuilder(construction::Manager* manager, int builderId);
void RemoveBuilder(construction::Manager* manager, int builderId);

} // namespace runtime
} // namespace obl::construction
