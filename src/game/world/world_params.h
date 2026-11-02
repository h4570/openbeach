#pragma once

#include "game/tables/world_params_tables.h"

namespace obl::world {

// Registers the call-drive fixture of the world-params reads below (world_params_queries.cpp, DLL only). The building and
// footprint data come from the converted catalog (game/tables/catalog_load.cpp, called by world_params::Construct).
bool RegisterWorldParamsDriver();

// World-params reads (world_params_queries.cpp): the entries of the id lookups and of the record
// accessors. Each counts its `world.params-<method>` call counter. A key missing from its
// table ends the game with a FatalError message, except GetSceneryId, which returns 0. The accessors do no
// bounds check.
namespace world_params {
// The lifecycle (world_params_process.cpp), plain calls from ConstructWorld and the world destructor. Construct makes the
// object with `new` and fills it from the pack (the guest set `guestList` names); Destroy deletes it.
// Current() is the object the last Construct made (null before it and after Destroy): the readers' replacement of the
// former g_WorldParams image global.
tables::WorldParams* Construct(const char* guestList);
void Destroy(tables::WorldParams* params);
tables::WorldParams* Current();

int GetCommodityFamilyId(tables::WorldParams* params, const char* key);
int GetCommodityId(tables::WorldParams* params, const char* key);
int GetBuildingId(tables::WorldParams* params, const char* key);
int GetEventId(tables::WorldParams* params, const char* key);
int GetBeerId(tables::WorldParams* params, const char* key);
int GetFineId(tables::WorldParams* params, const char* key);
int GetStaffId(tables::WorldParams* params, const char* key);
int GetGuestId(tables::WorldParams* params, const char* key);
int GetVehicleId(tables::WorldParams* params, const char* key);
int GetTableId(tables::WorldParams* params, const char* key);
int GetSceneryId(tables::WorldParams* params, const char* key);

const char* GetCommodityName(tables::WorldParams* params, int id);
int GetCommodityFamilyOf(tables::WorldParams* params, int id);
const char* GetCommodityDisplayName(tables::WorldParams* params, int id);
tables::FineDef* GetFineRecord(tables::WorldParams* params, int id);
tables::GuestDef* GetGuestDefinition(tables::WorldParams* params, int id);
tables::EventDef* GetEventDefinition(tables::WorldParams* params, int id);
tables::BeerDef* GetBeerDefinition(tables::WorldParams* params, int id);
tables::BuildingDef* GetBuildingDef(tables::WorldParams* params, int id);
const char* GetBuildingKey(tables::WorldParams* params, int id);
tables::StaffDef* GetStaffDefinition(tables::WorldParams* params, int id);
tables::VehicleDef* GetVehicleDefinition(tables::WorldParams* params, int id);
tables::TableDef* GetTableDefinition(tables::WorldParams* params, int id);
tables::SceneryDef* GetSceneryDef(tables::WorldParams* params, int index);
} // namespace world_params

} // namespace obl::world
