#pragma once

#include <vector>

namespace obl::object {

struct Building;
} // namespace obl::object

namespace obl::world_save {
struct BuildingEventsJson;
struct BuildingQueueJson;
struct BuildingThoughtJson;
} // namespace obl::world_save

namespace obl::object {

// The call-drive driver of the thought fixture of the obl.save load of a building
// (docs/architecture/objects.md).
bool RegisterBuildingThoughtDriver();

// The building Load's game allocations for an obl.save entry (save-switch-objects.md, ObjectLoadCallbacks): the schedule and the
// queue constructed for the building and filled from the entry, the thought list cleared and refilled.
void BuildingLoadEventsJson(object::Building& building, const world_save::BuildingEventsJson& in);
void BuildingLoadQueueJson(object::Building& building, const world_save::BuildingQueueJson& in);
void BuildingLoadThoughtsJson(object::Building& building, const std::vector<world_save::BuildingThoughtJson>& in);

} // namespace obl::object
