#pragma once

#include "game/repair/repair_state.h"

#include <cstdint>

namespace obl::repair {

// Registers the manager test drivers.
bool RegisterRepairTestDrivers();

namespace repair_facade {

// World construct and destruct (world_construct_subsystems.h, world_save.h DestructWorld): a plain `new`/`delete`.
repair::Manager* Create();
void Destroy(repair::Manager* manager);

void AddJob(repair::Manager* manager, int id, uint32_t priority);
void AddUrgentJob(repair::Manager* manager, int id);
void ScanJobs(repair::Manager* manager, repair::Scan* result, const float* point, int kind);
int FindJob(repair::Manager* manager, const float* point, int kind, uint32_t* typeOut);
int LookForBetterJob(repair::Manager* manager, const float* point, int currentId, uint32_t* typeOut);
int FindWateringJob(repair::Manager* manager, const float* point);
void ClaimJob(repair::Manager* manager, int id, uint32_t urgent, uint32_t type);
void ReleaseJob(repair::Manager* manager, int id);
void StartedJob(repair::Manager* manager, int id);
void ReturnJob(repair::Manager* manager, int id, uint32_t type);
int FindPlantToWater(repair::Manager* manager, const float* point, uint32_t radius);
int ClaimObject(repair::Manager* manager, int id, uint32_t* typeOut);
char GetJobStatus(repair::Manager* manager, int id);
int GetJobType(repair::Manager* manager, int id);
void RemoveJob(repair::Manager* manager, int id);
uint32_t CountUrgentJobs(repair::Manager* manager);

} // namespace repair_facade
} // namespace obl::repair
