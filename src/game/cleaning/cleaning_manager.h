#pragma once

#include "game/cleaning/cleaning_state.h"

#include <cstdint>

namespace obl::cleaning {

// Registers the manager test drivers.
bool RegisterCleaningTestDriver();

namespace cleaning_facade {

// World construct and destruct (world_construct_subsystems.h, world_save.h DestructWorld): a plain `new`/`delete`.
cleaning::Manager* Create();
void Destroy(cleaning::Manager* manager);

void AddJob(cleaning::Manager* manager, int id);
void AddUrgentJob(cleaning::Manager* manager, int id);
void ClaimJob(cleaning::Manager* manager, int id, uint32_t urgent);
void ReleaseJob(cleaning::Manager* manager, int id);
void CancelJob(cleaning::Manager* manager, int id);
void RemoveJob(cleaning::Manager* manager, int id);
int GetJobStatus(cleaning::Manager* manager, int id);
void StartedCleaning(cleaning::Manager* manager, int id);
void DidNotClean(cleaning::Manager* manager, int id);
int ClaimObject(cleaning::Manager* manager, int id);
int FindUrgentJob(cleaning::Manager* manager, int* typeOut, bool bins, bool scenery, bool buildings);
int FindBestScoredJob(cleaning::Manager* manager, const float* point, int* typeOut, bool bins, bool litter, bool toilets);

} // namespace cleaning_facade
} // namespace obl::cleaning
