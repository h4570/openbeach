#pragma once

#include "core/char16.h"
#include <cstdint>

namespace obl::object {

struct Building;
}

namespace obl::object {

// Building (docs/architecture/objects.md): the opening state (IsUserOrPowerClosedDuringHours, GetPowerOutput,
// UpdateOpenState, Open, Close, SwitchOffConsumer; decisions in `src/game/objects/building_open_state.h`), the visibility of the staff, the performers and the
// table waiters (HideStaffAndEvents / ShowStaffAndEvents, `src/game/objects/building_staff_visibility.h`), the wear (ApplyLightningDamage, SetBroken;
// `src/game/objects/building_breakdown.h`), Update, OnEnterState, OnExitState, ReceiveMessage, the vehicle, table and staff spawning and the variables.

// The building's own steps (each counts its `<name>` call counter).
void BuildingSpawnVehicles(object::Building* building);
void BuildingSpawnTables(object::Building* building);
bool BuildingHireStaff(object::Building* building);
void BuildingSpawnInitialStaff(object::Building* building);
void BuildingDismissStaff(object::Building* building, uint32_t staffId);
void BuildingHideStaffAndEvents(object::Building* building);
void BuildingShowStaffAndEvents(object::Building* building);
bool BuildingIsUserOrPowerClosedDuringHours(const object::Building* building);
void BuildingUpdateOpenState(object::Building* building);
// OnExitState (counted `objects.building-on-exit-state`); the test drivers run the STATE_STARTUP exit through it.
bool BuildingOnExitState(object::Building* building);
void BuildingOpen(object::Building* building);
void BuildingClose(object::Building* building);
int BuildingGetPowerOutput(object::Building* building);
void BuildingShedPower(object::Building* building);
void BuildingSetBroken(object::Building* building);
void BuildingApplyLightningDamage(object::Building* building);

// Fixture values deliberately bypass normal gameplay limits and job creation.
void SetBuildingVisitsSinceCleaned(object::Building& building, int visits);
void SetBuildingAge(object::Building& building, int age);
void SetBuildingCommodityPrices(object::Building& building, float price);
// Close the flag animation and request a fresh influence-area poll; false if no flag is available.
bool PollBuildingClosedFlag(object::Building& building);
// Complete startup through the counted exit handler, then return to the working state.
void CompleteBuildingStartup(object::Building& building);
// Arm leaves inspection settings untouched when interval is zero; reset preserves jobs and fault history.
void ArmBuildingFailure(object::Building& building, int hours, int interval);
void ResetBuildingWear(object::Building& building);

// (docs/architecture/objects.md): the small queries and setters (positions, serve point, queue direction, open flag, average intensities,
// build/demolish job, thought id) are the building_api:: facades in `src/game/objects/building_api.h`; no install step.

// The thought list of a building (AddThought, RemoveThoughtsOf), counting the `<name>` counters; the logic is in
// `src/game/objects/building_thoughts.h`.
void BuildingAddThought(object::Building* building, Char16* text, int32_t guestId);
void BuildingRemoveThoughtsOf(object::Building* building, int32_t objectId);

} // namespace obl::object
