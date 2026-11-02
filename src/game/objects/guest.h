#pragma once

#include "core/call_counter.h"
#include "game/tables/world_params_leaves.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace obl::messages {
struct Message; // message_state.h
}

namespace obl::object {

struct Building;
struct Mover;
struct Guest;
} // namespace obl::object
namespace obl::object {
struct Base;
} // namespace obl::object
namespace obl::layout {
using tables::BuildingDef;
using Message = messages::Message;
} // namespace obl::layout

namespace obl::object {

// Guest / guest function of the guest simulation (docs/architecture/objects.md).
//
// UpdateDesires, ApplyWeatherModifiers, UpdateOpinions, the six visit/spot opinion helpers, CheckBedTime, CheckLeaveResort, the beach helpers SetRepeatAction,
// FindSunbathePoint, FindSwimPoint and DecideSwimAction, UpdateDrowning, StartStomachIllness, FindFightPoint, FindFightPerson, TellNearbyOfFight, LookForVandalItem, VandaliseBin, the seven
// thought functions AddIllPurchaseThought, AddCommodBeachClosed, AddThoughtIDidnt, AddThoughtIDidIt, AddDrowningThought, NoRouteToLodging, AddBuildingFullThought, DemolishedAsleep,
// GenerateBlockedMessage, MissedDesire, AlterDesires, SetupStartValues, the palettes RandomizePalette and RandomizeAttachedPalette, the getters GetPaletteTone,
// GetFoodIllness, GetSunburnSeverity, IsApproachable and HasState, SetupSnogOffset, FaceEachOther, FindLodging, SetSurfacePoint and IsEntryPointClear, and the walking
// goals HeadForEntryPoint, GetSeaEntry, GotoWorldUnloadPoint, WalkToWindsurf, StartPathWalk, SurfToShore, WindSurfStraight, GotoDancePoint and HeadForBin, TellNearbyOfTopless and
// SendOutDeathMessage, and the lookups LookForIllPoint, LookForBench, QueueAtComplaints, QueueAtWharf, CreateLitter, GetConsumePoint, FindFlirtTarget and MoveActionPerson, and the grid and pool movers FindNearestSeaPoint, WanderBeach, GetNextPoolPoint, ChooseFunSlideAction, SetupFunPoolSwim and WalkAlongPath, the animation call PlayAnimation and the desire search SearchDesire (docs/architecture/objects.md).
bool RegisterGuestTestDrivers();
void RegisterGuestLifecycleDriver();
void SetGuestClearSelectionProbe();
void ForceGuestSelection();
void RegisterGuestDrawDrivers();
void ForceGuestVariableText();
void ForceGuestAppearance(); // test helpers: calls the natural run does not make (tests/guest.txt)
void ForceGuestPlacement();
void RegisterGuestSoundDriver();

// Adds food illness (counted as `objects.guest-add-food-ill`): the food illness grows by `amount` (the appearance rule).
void GuestAddFoodIll(object::Guest& guest, float amount);

// Pushes the thought `prefix` + rand() % 3 + 1 (a TextTable key) onto the guest's three thoughts when the key exists; returns its id or -1.
int GuestThought(object::Guest& guest, const std::string& prefix);

// The pieces of a refused visit that src/game/objects/guest_opinion_helpers.cpp and src/game/objects/guest_thoughts.cpp share (the same calls in the same order).
object::Base* FindObjectById(int id); // object lookup by id
const char* GuestCommodityName(const tables::BuildingDef& def, int slot); // Commodity key lookup of the slot's commodity
void GuestBuildingThought(object::Guest& guest, object::Building* building,
        int textId); // the text of textId added as a thought of the building; null building: nothing
void GuestBadVisitMood(object::Guest& guest); // last-visit flags and the mood icon
void GuestBadVisitSound(object::Guest& guest); // "bad visit" opinion sound (male 8, female 15)
void GuestStateChange(object::Guest& guest, int state); // immediate MT_STATECHANGE to itself
void GuestPickBarTile(object::Guest& guest, int32_t& x,
        int32_t& y); // the walkable tile around the nearest open bar (FindFightPoint's search; draws x then y)
bool GuestBarGroundWalkable(object::Base* building); // test drivers: walkable ground within seven tiles, or the bar search never ends
float* GuestFindNearestSeaPoint(float x, float y); // FindNearestSeaPoint (the guest and z are unused; counted by a call counter)
bool GuestIsEntryPointClear(object::Guest& guest); // IsEntryPointClear (counted as `objects.guest-is-entry-point-clear`)
double GuestPaletteTone(object::Guest& guest); // counted by a call counter, as are the next two
float GuestFoodIllness(object::Guest& guest);
bool GuestIsApproachable(object::Guest& guest);
// Counted as `objects.guest-get-sunburn-severity`.
float GuestSunburnSeverity(const object::Guest& guest);
// Counted by a call counter: a guest without a sprite stops the game.
bool GuestTrySelectAtScreenPoint(object::Guest& guest, int32_t* screenPoint);

// The guest's own entries are Guest members (object_guest.h): Guest::CallFunction reaches the delegated ones through the
// table in guest_script.cpp (ids in guest_call_table.h), the desire search and the test drivers call them directly.
float* GuestFindSwimPoint(float x, float y); // FindSwimPoint

// Test seams of the guest-desire trace (testing/guest_desires.cpp): the calls of the game (function id, SearchDesire, the script's
// FindSunbathePoint) reach these through the pointers, which start at the members above; the trace puts its probes in and forwards
// to the previous value. Calls inside the entries themselves stay direct.
void GuestApplyWeatherModifiers(object::Guest* guest);

struct GuestSeams {
    void (*updateDesires)(object::Guest* guest);
    void (*updateOpinions)(object::Guest* guest);
    void (*alterQueueOpinion)(object::Guest* guest);
    void (*alterSafetyOpinion)(object::Guest* guest);
    void (*updatePriceScore)(object::Guest* guest);
    void (*alterPriceOpinion)(object::Guest* guest);
    void (*updatePurchaseOpinion)(object::Guest* guest);
    void (*doSpotOpinion)(object::Guest* guest);
    bool (*checkBedTime)(object::Guest* guest);
    bool (*checkLeaveResort)(object::Guest* guest);
    void (*updateDrowning)(object::Guest* guest);
    void (*decideSwimAction)(object::Guest* guest);
    bool (*startBadGuts)(object::Guest* guest);
    void (*lookForVandalItem)(object::Guest* guest);
    void (*vandaliseBin)(object::Guest* guest);
    void (*findFightPoint)(object::Guest* guest);
    void (*findFightPerson)(object::Guest* guest);
    void (*sendOutFightingMsgs)(object::Guest* guest);
    void (*setRepeatAction)(object::Guest* guest, int desire, float value);
    float* (*findSunbathePoint)(object::Guest* guest);
    float* (*findSwimPoint)(float x, float y, float z); // z is unused (GuestFindSwimPoint)
    void (*applyWeatherModifiers)(object::Guest* guest);
};
extern GuestSeams g_guestSeams;

} // namespace obl::object
