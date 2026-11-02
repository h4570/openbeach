#pragma once

#include "core/ascii_case.h"
#include "game/tables/building_behavior.h"
#include "game/objects/building_update.h"
#include "game/messages/message_state.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/objects/object_vehicle.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>
#include <cstring>

// The start and the end of a building's life as Building sees it (docs/architecture/objects.md): OnEnterState, OnExitState
// and the ReceiveMessage cases of the building alone that need no game call or only a lookup (docs/architecture/objects.md). No game memory here:
// the glue in src/game/objects/building_methods.cpp implements every Env with the game calls, in the order the code below asks for them.
namespace obl {

inline bool NameIs(const char* name, const char* expected)
{
    return ascii::EqualsNoCase(name, expected);
}

// The world values the three functions read.
struct LifecycleWorld {
    bool startup = false; // the world is being built (a loaded game or a scenario), not played yet
    float powerUse = 0.0f;
    float maxPower = 0.0f;
    float currentPower = 0.0f;
    float gameTime = 0.0f;
    int32_t gameDay = 0;
    float gameDayTime = 0.0f;
};

// ---- the animations of the sprite -------------------------------------------------------------------------------------------------------

// The index of the animation called `name` in the sprite's list, -1 when there is none (a case-sensitive compare of the names).
inline int32_t FindAnimIndex(const ui::Sprite& sprite, const char* name)
{
    for (int32_t i = 0; i < sprite.m_animCount; ++i) {
        if (std::strcmp(sprite.m_anims[i]->m_name, name) == 0) return i;
    }
    return -1;
}

// every ANIMSPRITE of the building (the lamps, the flag,...) follows the direction of the building's own sprite: the direction is
// taken modulo the number of directions of the part's first animation (a signed remainder) and cut to the number of directions of the animation the part
// is playing. A part whose first animation has no direction keeps remainder 0 instead of dividing by zero.
inline void AlignAnimSprites(object::Building& building)
{
    const auto* own = static_cast<const ui::SpriteInstance*>(building.m_sprite);
    const uint32_t parts = static_cast<uint32_t>(building.m_definition->m_NumAnimSprites);
    for (uint32_t i = 0; i < parts; ++i) {
        ui::SpriteInstance* part = building.m_animSprites[i];
        const ui::Sprite& sprite = *part->m_sprite;
        const int32_t directions = sprite.m_anims[0]->m_directions;
        const int32_t direction = directions != 0 ? own->m_direction % directions : 0;
        const int32_t limit = sprite.m_anims[part->m_animIndex]->m_directions;
        part->m_direction = direction < limit ? direction : limit - 1;
    }
}

// ---- OnEnterState ---------------------------------------------------------------------------------------------------------------------

// What one OnEnterState call did (the branch counters of the handler).
struct EnterReport {
    bool beachHut = false; // state 0: the ten lounger searches
    bool working = false; // state 0
    bool pool = false; // state 0 of the main pool: the ball position
    bool startup = false; // state 1
    bool noBuilders = false; // state 1: the player is told that no builder is hired
    bool demolish = false; // state 2
    bool demolished = false; // state 3
    bool areaOpened = false; // state 3 of the last lifeguard station of a swimming area
};

// The offset of the pool's ball from its footprint (two float constants, -208 and -496).
inline constexpr float kPoolBallOffsetX = -208.0f;
inline constexpr float kPoolBallOffsetY = -496.0f;

struct LoungerTile {
    uint32_t x, y;
};

// Multiply by 1/32767 as a float, then by 20, then truncate: keep both products
// in double (53-bit precision), without a float store.
inline int32_t LoungerOffset(int draw)
{
    return static_cast<int32_t>(static_cast<double>(draw) * 0x1.0002p-15f * 20.0) - 10;
}

template<class Env> void PlaceBeachHutLoungers(Env& env)
{
    for (int lounger = 0; lounger < 10; ++lounger) {
        int retries = 0;
        for (;;) {
            const LoungerTile origin = env.BuildingTile();
            const uint32_t x = origin.x + static_cast<uint32_t>(LoungerOffset(env.Rand()));
            const uint32_t y = origin.y + static_cast<uint32_t>(LoungerOffset(env.Rand()));
            LoungerTile neighbours[8];
            for (int direction = 0; direction < 8; ++direction) neighbours[direction] = env.LoungerNeighbour({x, y}, direction);
            // The four cardinal neighbours are tested before the diagonals.
            constexpr int order[] = {0, 2, 4, 6, 1, 3, 5, 7};
            const auto eligible = [&](LoungerTile tile) {
                const uint32_t flags = env.LoungerFlags(tile);
                return (flags & 2) != 0 && (flags & 0x8010) == 0;
            };
            bool valid = eligible({x, y});
            for (int direction : order) {
                if (!valid) break;
                valid = eligible(neighbours[direction]);
            }
            // Even a valid 100th retry is discarded.
            if (retries == 100) break;
            if (valid) {
                env.PlaceLounger({x, y});
                break;
            }
            ++retries;
        }
    }
}

// OnEnterState(state). The Env gives, in call order:
//   void NotifyNoBuilders()                    FEEDBACK_NO_BUILDERS with the building's name, type 4 (feedback::Feedback::AddText)
//   int CountUsableBuilders()                  counting available builders
//   void UpdateOpenState()                     the building's UpdateOpenState
//   void StartBuild() / StartDemolish() / FinishDemolish()   updates the building construction or demolition job
//   int CountSwimmingAreaKeepers(int32_t area) the BAYWATCH objects of the world whose m_InfluenceArea is `area`
//   void OpenSwimmingArea(int32_t area)        opens the beach area, then its paired sea area
//   void PoolBallPosition(float&, float&)      the footprint point at the offsets above
//   void BuildingSoundStart()                  starting the building sound
// BEACHHUT also uses BuildingTile, Rand, LoungerNeighbour, LoungerFlags and PlaceLounger.
template<class Env> EnterReport EnterState(Env& env, object::Building& building, uint32_t state, const LifecycleWorld& world)
{
    EnterReport report;
    switch (state) {
    case 0:
        if (building.m_definition->m_Behavior == BuildingBehavior::BeachHut) {
            report.beachHut = true;
            PlaceBeachHutLoungers(env);
        }
        report.working = true;
        if (building.m_definition->m_Behavior == BuildingBehavior::MainPool) {
            report.pool = true;
            float x = 0.0f, y = 0.0f;
            env.PoolBallPosition(x, y);
            building.m_BallPosition[0] = x;
            building.m_BallPosition[1] = y;
            building.m_BallPosition[2] = 0.0f;
        }
        env.BuildingSoundStart();
        building.m_soundPlaying = 1;
        return report;
    case 1: {
        report.startup = true;
        const auto* own = static_cast<const ui::SpriteInstance*>(building.m_sprite);
        building.m_shadowOffset = FindAnimIndex(*own->m_sprite, "shadow");
        building.m_pickOffset = FindAnimIndex(*own->m_sprite, "pick");
        building.m_lightMapOffset = FindAnimIndex(*own->m_sprite, "lightmap");
        AlignAnimSprites(building);
        if (!world.startup) {
            if (env.CountUsableBuilders() == 0) {
                env.NotifyNoBuilders();
                report.noBuilders = true;
            }
        } else {
            env.UpdateOpenState();
        }
        building.m_buildingAnimTime = world.gameTime;
        env.StartBuild();
        return report;
    }
    case 2:
        report.demolish = true;
        building.m_builtByUser = 0;
        env.StartDemolish();
        return report;
    case 3:
        report.demolished = true;
        env.FinishDemolish();
        if (building.m_InfluenceArea != -1) {
            // The lifeguards of one swimming area share it: the last one to go gives the area back (the building itself still counts).
            if (env.CountSwimmingAreaKeepers(building.m_InfluenceArea) > 1) return report;
            env.OpenSwimmingArea(building.m_InfluenceArea);
            report.areaOpened = true;
        }
        return report;
    default: return report;
    }
}

// ---- OnExitState ----------------------------------------------------------------------------------------------------------------------

// The lifeguard station's flag (the first ANIMSPRITE) shows whether its swimming area is open: animation 0 for open, 1 for closed; the frame restarts
// only when the animation changes. OnExitState (BAYWATCH block) and MT_RE_POLL_FLAG_STATE do the same thing.
inline bool ShowSwimmingAreaFlag(object::Building& building, bool open)
{
    ui::SpriteInstance* flag = building.m_animSprites[0];
    const int32_t anim = open ? 0 : 1;
    if (flag->m_animIndex == anim) return false;
    flag->m_animIndex = anim;
    flag->m_frameIndex = 0;
    return true;
}

// What one OnExitState call did.
struct ExitReport {
    bool startupDone = false; // the building left state 1 (the only state OnExitState does anything for)
    bool swimmingArea = false; // BAYWATCH: the swimming area was found
    bool reselected = false; // the building was selected: its dialog was closed and opened again
    bool finished = false; // FinishBuild (a building of a world being built, not one the player paid for)
    bool shed = false; // the world has less power than it uses: the new building is shut at once
    bool stage = false; // the STAGE got its show schedule
    bool inspection = false; // a building the player built with a failure chance asked for its first inspection
};

// OnExitState. Leaving the building site (state 1) brings the finished building up: the swimming area of a lifeguard station, the staff, vehicles
// and tables, the queue, the power check, the show schedule of the stage and the first inspection. The Env gives, in call order:
//   int FindSwimmingArea()                   the beach area containing m_WorldXYZ
//   bool IsSwimmingAreaOpen(int32_t area)    whether the corresponding sea area is open
//   void SpawnInitialStaff() / SpawnVehicles() / SpawnTables()
//   void CloseDialog() / Reselect()          closing the provider dialog, OnSelect(clicked 1, centerCamera 0)
//   void FinishBuild()                       marks the building construction job complete
//   void* CreateQueue()                      creates the queue with its capacity, serve point, direction and building id
//   void HideStaff() / ClearQueueForClosing() / PostClosing()   the three steps of shedding the building's power, inlined
//   object::BuildingEvents* CreateEvents()   creates the building event schedule
//   void RequestInspection()                 repair::repair_facade::AddJob(repair queue, id, 0)
template<class Env> ExitReport ExitState(Env& env, object::Building& building, const LifecycleWorld& world)
{
    ExitReport report;
    if (building.m_state != kBuildingStateStartup) return report;
    report.startupDone = true;
    if (building.m_definition->m_Behavior == BuildingBehavior::LifeguardPost) {
        report.swimmingArea = true;
        building.m_InfluenceArea = env.FindSwimmingArea();
        ShowSwimmingAreaFlag(building, env.IsSwimmingAreaOpen(building.m_InfluenceArea));
    }
    env.SpawnInitialStaff();
    env.SpawnVehicles();
    env.SpawnTables();
    if (building.m_SelectLevel == 1) {
        env.CloseDialog();
        env.Reselect();
        report.reselected = true;
    }
    if (world.startup) {
        env.FinishBuild();
        report.finished = true;
    }
    building.m_queue = env.CreateQueue();
    if (world.powerUse > world.currentPower) {
        // The handyman's hut is the one building that must work whenever there is power for it to draw; the others, when they need any.
        const bool spared = (building.m_definition->m_Behavior == BuildingBehavior::MechanicHome && world.currentPower < world.maxPower) ||
                building.m_definition->m_Power <= 0;
        if (spared) {
            building.m_closedForPower = 0;
        } else {
            building.m_closedForPower = 1;
            building.m_OpenStatus = 1;
            env.HideStaff();
            env.ClearQueueForClosing();
            env.PostClosing();
            report.shed = true;
        }
    }
    if (building.m_definition->m_Behavior == BuildingBehavior::Stage) {
        building.m_events = env.CreateEvents();
        report.stage = true;
    }
    if (!world.startup) {
        building.m_builtByUser = 1;
        if (building.m_definition->m_FailPercent != 0) {
            env.RequestInspection();
            report.inspection = true;
        }
    }
    return report;
}

// ---- ReceiveMessage: MT_REPAIR and MT_EVENT_FINISHED ---------------------------------------------------------------------------------

// What one MT_REPAIR did.
struct RepairReport {
    bool more = false; // the building is still not sound: MT_MORE_REPAIRS went back to the mechanic
    bool finished = false; // MT_REPAIRS_FINISHED went back
    bool notified = false; // the player was told that a broken building works again
    bool noPower = false; // ... in the version for a building that is closed for lack of power
};

// MT_REPAIR (0x27) from a mechanic: one visit takes the wear down one step (3 -> 2 -> 1 -> 0; a value above 3 goes to 2). The mechanic is sent
// MT_MORE_REPAIRS while the wear is not gone, MT_REPAIRS_FINISHED when it is; then the mechanic's claim on the building and the calls for a
// mechanic are cleared, a building that had broken down tells the player (FEEDBACK_BUILDING_REPAIRED, or ..._NOPOWER when it is shut for power) and the
// time of the last repair is stamped. The Env gives:
//   void Post(const messages::Message&)         the building's virtual Send, a post to the message handler
//   void NotifyRepaired(bool noPower)         feedback::Feedback::AddText(feedback queue, text, id, 4)
template<class Env>
RepairReport RepairMessage(Env& env, object::Building& building, const messages::Message& message, const LifecycleWorld& world)
{
    RepairReport report;
    uint32_t& wear = building.m_RepairState;
    if (wear != 0) wear = wear == 1 ? 0 : (wear != 2) + 1;

    messages::Message reply{};
    reply.sender = static_cast<int32_t>(building.m_id);
    reply.receiver = message.sender;
    if (wear != 0) {
        reply.type = messages::MT_MORE_REPAIRS;
        env.Post(reply);
        report.more = true;
        return report;
    }
    reply.type = messages::MT_REPAIRS_FINISHED;
    env.Post(reply);
    report.finished = true;
    building.m_repairing = 0; // the mechanic's claim is released
    building.m_calledRepairing = 0;
    building.m_urgentRepairCalled = 0;
    building.m_calledForRepair = 0;
    building.m_calledForInspection = 0;
    if (building.m_wasBroken) {
        report.noPower = building.m_closedForPower != 0;
        env.NotifyRepaired(report.noPower);
        building.m_wasBroken = 0;
        report.notified = true;
    }
    building.m_lastRepairDayTime = world.gameDayTime;
    building.m_LastRepairDay = world.gameDay;
    return report;
}

// ---- ReceiveMessage: the bookkeeping messages (STAFF_CREATED, NEW_DAY, RE_POLL_FLAG_STATE, TABLE_CREATED, LOUNGER_CREATED) --------------------------

// What one MT_STAFF_CREATED did.
struct StaffCreatedReport {
    bool serving =
            false; // data == 0: one of the staff that serve the guests (the building's definition lists their kinds); else a performer/cleaner
    bool female = false; // serving: the new member is counted with the women
    bool hidden = false; // the building is closed and the new member is a service-staff object: made invisible
};

// MT_STAFF_CREATED (0x25): a staff member the building asked for exists (the initial staff spawn, hiring, a performer's script). `data` == 0 is
// serving staff: appended to m_staff, its busy flag cleared, counted as male or female by the StaffDef the building's definition lists for that slot (the slot
// is the number of serving staff so far; the definition has ten at most and nothing checks that). Any other `data` is appended to
// the non-serving list. When the building is closed (m_OpenStatus 1) a new member of ServiceStaff class is hidden at once, like the rest of the staff.
// The Env gives: `bool IsFemaleStaff(int32_t staffDefinitionId)`, `void HideServiceStaff(int32_t objectId)` (hides the object when its class is
// ServiceStaff; the object is looked up without a null check).
template<class Env> StaffCreatedReport StaffCreated(Env& env, object::Building& building, const messages::Message& message)
{
    StaffCreatedReport report;
    if (message.data == 0) {
        report.serving = true;
        const int32_t slot = building.m_numMalestaff + building.m_numFemalestaff;
        building.m_staff[slot] = message.sender;
        building.m_staff_busy[slot] = 0;
        report.female = env.IsFemaleStaff(building.m_definition->m_Staff[slot]);
        if (report.female)
            ++building.m_numFemalestaff;
        else
            ++building.m_numMalestaff;
    } else {
        building.m_NonServeStaff[building.m_NumNonServestaff] = message.sender;
        ++building.m_NumNonServestaff;
    }
    if (building.m_OpenStatus == 1) {
        env.HideServiceStaff(message.sender);
        report.hidden = true;
    }
    return report;
}

// MT_NEW_DAY (0x26), a building that works (state 0): the day's books are closed. The loops are kept as they are, odd as they look (tests pin them):
//  - the profit record is "shifted" by a forward copy (`record[i] = record[i - 1]` upward from entry 1), which spreads the old entry 0 over the whole
//    table instead of moving it (a bug of the game, kept: saves and the world hash depend on it); entry 0 then takes today's profit;
//  - the age grows by one and today's income, cost, profit and the counters of visits and cleanings restart (the visits since the last cleaning stay);
//  - the average profit is the sum of the first min(age, 64) entries divided by the age (unsigned, also beyond 64 entries; computed in double);
//  - each commodity's male/female intensity is scaled by the definition's daily factor C and its price factor by E.
// Returns true when the message was handled (state 0).
inline bool NewDay(object::Building& building)
{
    if (building.m_state != kBuildingStateNull) return false;
    float* record = building.m_profitRecord;
    for (int i = 1; i < 64; ++i) record[i] = record[i - 1];
    record[0] = building.m_profitToday;
    const uint32_t age = static_cast<uint32_t>(building.m_BuildingAge) + 1;
    building.m_profitToday = 0.0f;
    building.m_incomeToday = 0.0f;
    building.m_costToday = 0.0f;
    building.m_NumVisitsToday = 0;
    building.m_BuildingAge = static_cast<int32_t>(age);
    building.m_NumTimesCleanedToday = 0;
    if (age != 0) {
        double sum = 0.0;
        for (uint32_t i = 0; i < age && i < 64; ++i) sum += record[i];
        building.m_averageProfit = static_cast<float>(sum / static_cast<double>(age));
    }
    const tables::BuildingDef& def = *building.m_definition;
    for (uint32_t i = 0; i < static_cast<uint32_t>(def.m_NumCommodities); ++i) {
        building.m_Commod_Intensity[i][0] *= def.m_CommodityC[i];
        building.m_Commod_Intensity[i][1] *= def.m_CommodityC[i];
        building.m_Commod_PriceFactor[i] *= def.m_CommodityE[i];
    }
    return true;
}

// MT_RE_POLL_FLAG_STATE (0x2f): a lifeguard station with a swimming area looks at the area again and sets its flag. Env: `bool IsSwimmingAreaOpen(area)`.
// Returns true when the flag changed.
template<class Env> bool RePollFlagState(Env& env, object::Building& building)
{
    if (building.m_InfluenceArea == -1) return false;
    return ShowSwimmingAreaFlag(building, env.IsSwimmingAreaOpen(building.m_InfluenceArea));
}

// MT_TABLE_CREATED (0x50) and MT_LOUNGER_CREATED (0x51): the table/lounger objects register themselves with the building that owns them.
inline void TableCreated(object::Building& building, const messages::Message& message)
{
    building.m_Tables[building.m_numTables] = message.sender;
    ++building.m_numTables;
}
inline void LoungerCreated(object::Building& building, const messages::Message& message)
{
    building.m_loungers[building.m_numLoungers] = message.sender;
    ++building.m_numLoungers;
}

// ---- ReceiveMessage: the vehicles (GUEST_LEFT_VEHICLE, VEHICLE_CREATED, BOARD_VEHICLE) and WAIT_FOR_BOAT -------------------------------------
//
// The hire buildings (jet skis, pedal boats, speed boats, fishing trips) own up to five vehicles; each is paired with the table of the same index (the
// place the guest stands on while waiting; `m_Tables[i]` belongs to `m_Vehicles[i]`). The vehicles introduce themselves with VEHICLE_CREATED, a guest
// tells the building that it got out with GUEST_LEFT_VEHICLE, and BOARD_VEHICLE is what the building's queue sends a waiting guest. The messages the
// building sends go through its own Send, posting to the message handler: `void Post(const messages::Message&)` in the Env.

// The word at `index` after m_Vehicles. BOARD_VEHICLE walks the tables and takes the vehicle of the same index without a range check; a building with more
// tables than the six vehicle slots reads the in-use flags and what follows them (the object is far bigger than the array).
inline int32_t VehicleSlotWord(const object::Building& building, uint32_t index)
{
    int32_t value;
    std::memcpy(&value, reinterpret_cast<const uint8_t*>(building.m_Vehicles) + index * sizeof(int32_t), sizeof(value));
    return value;
}

// MT_VEHICLE_CREATED (0x4c, sender = the new vehicle): the vehicle becomes the next of the building's list (free, not in use) and is given its own
// position as the first stop of its route (the vehicle's AddRouteStop; the position is read from the vehicle's object, which is
// not checked for null). The Env gives: `void AddRouteStop(int32_t vehicleId)`. No bound on the list: the definition has five vehicles.
template<class Env> void VehicleCreated(Env& env, object::Building& building, const messages::Message& message)
{
    const int32_t slot = building.m_NumVehicles;
    building.m_Vehicles[slot] = message.sender;
    building.m_vehicleInUse[slot] = 0;
    ++building.m_NumVehicles;
    env.AddRouteStop(message.sender);
}

// What one MT_GUEST_LEFT_VEHICLE did.
struct GuestLeftReport {
    bool matched = false; // the vehicle (data) is one of the building's
    bool seated =
            false; // the building has tables: the vehicle's table took the guest (the table's SeatGuest) instead of the guest being told its state
};

// MT_GUEST_LEFT_VEHICLE (0x34, sender = the guest, data = the vehicle the guest got out of): the first listed vehicle with that id decides. Without tables
// the guest is sent MT_STATECHANGE with state 0x1c (the guest goes on with its day); with tables the table of the same index seats the guest
// (the table's SeatGuest(guest); the table object is not checked for null). Later vehicles are not looked at. Env:
// `bool SeatGuest(int32_t tableId, int32_t guestId)` (the result is looked at by MT_IM_READY only).
template<class Env> GuestLeftReport VehicleLeft(Env& env, object::Building& building, const messages::Message& message)
{
    GuestLeftReport report;
    for (uint32_t i = 0; i < static_cast<uint32_t>(building.m_NumVehicles); ++i) {
        if (message.data != building.m_Vehicles[i]) continue;
        report.matched = true;
        if (building.m_numTables == 0) {
            messages::Message state{};
            state.sender = static_cast<int32_t>(building.m_id);
            state.receiver = message.sender;
            state.type = messages::MT_STATECHANGE;
            state.data = 0x1c;
            env.Post(state);
        } else {
            env.SeatGuest(building.m_Tables[i], message.sender);
            report.seated = true;
        }
        break;
    }
    return report;
}

// MT_BOARD_VEHICLE (0x4e, sender = the table, data = the guest that boards): for every table with that id (the loop goes on after a match) the guest is
// told to change state (MT_STATECHANGE, state 0xf) and the vehicle of the same index is told to serve it (MT_SERVE_GUEST, data = the guest).
// Returns the number of tables that matched.
template<class Env> int BoardVehicle(Env& env, object::Building& building, const messages::Message& message)
{
    int matched = 0;
    for (int32_t i = 0; i < static_cast<int32_t>(building.m_numTables); ++i) {
        if (message.sender != building.m_Tables[i]) continue;
        messages::Message state{};
        state.sender = static_cast<int32_t>(building.m_id);
        state.receiver = message.data;
        state.type = messages::MT_STATECHANGE;
        state.data = 0xf;
        env.Post(state);
        messages::Message serve{};
        serve.sender = static_cast<int32_t>(building.m_id);
        serve.receiver = VehicleSlotWord(building, static_cast<uint32_t>(i));
        serve.type = messages::MT_SERVE_GUEST;
        serve.data = message.data;
        env.Post(serve);
        ++matched;
    }
    return matched;
}

// MT_WAIT_FOR_BOAT (0x45, sender = the guest): the guest joins the building's queue (Enqueue(queue, guest, 0, moveGuest); a missing queue is
// not checked for). The queue walks the guest to its place (`moveGuest`) except at a WHARF or BUSSTOP (the definition's key) and at the
// building named COMPLAINTHUT, where the guest stays where it stands. Env: `void Enqueue(int32_t guestId, bool moveGuest, int32_t slot)`. Returns `moveGuest`.
template<class Env> bool WaitForBoat(Env& env, object::Building& building, const messages::Message& message)
{
    const BuildingBehavior behavior = building.m_definition->m_Behavior;
    const bool moveGuest =
            behavior != BuildingBehavior::ShipStop && behavior != BuildingBehavior::BusStop && behavior != BuildingBehavior::ComplaintsHut;
    env.Enqueue(message.sender, moveGuest, 0);
    return moveGuest;
}

// ---- ReceiveMessage: guests and goods --------------------------------------------------------------------------------------
//
// MT_LOOK_AROUND (0x54), MT_COMMOD_QUALITY (0x32) and MT_GUEST_SERVED (0x53). The building tells a guest that it can be served with MT_IM_READY (0x1e:
// sender = the building, data = the id of whoever does the serving, floatData = the price of the good the guest asked for; the guest reads them in its
// script). Every message the building sends goes through Send (`env.Post`) and SendDelayed (`env.PostDelayed`, with a delay in seconds), which are
// the message handler's Post and PostDelayed (SendMessage / SendMessageDelayed). The words after data of the records are
// zeroed.

inline messages::Message BuildingMessage(const object::Building& building, int32_t receiver, int32_t type, int32_t data = 0)
{
    messages::Message message{};
    message.sender = static_cast<int32_t>(building.m_id);
    message.receiver = receiver;
    message.type = type;
    message.data = data;
    return message;
}

// The kind of a rental good a MT_COMMOD_QUALITY counted.
enum class RentalGood { None, Snorkel, Lilo, Board };

// MT_LOOK_AROUND, the building's turn to look around: a building that has been visited more than 25 times since it was last cleaned (m_VisitsSinceCleaned, unsigned)
// shows its state to everyone within two tiles: every guest there (type 3) gets MT_SPOT_OPINION (0x49, data 0), then the building wakes itself up again with
// MT_LOOK_AROUND after 2 s (the message that survives ClearDelayed, game/messages/message_logic.h). Below the limit nothing happens and the loop stops (the cleaner's job
// restarts it, the building's Update). Env: `uint32_t ObjectsNear(int32_t radius, object::Base* const*& list)` (the object manager's near-tile list around the
// building's tile; the list is the manager's, read while the messages are only queued), `void Post(const Message&)`, `void PostDelayed(const Message&, float)`.
struct LookAroundReport {
    bool looked = false; // over the limit
    uint32_t guestsNotified = 0;
};

inline constexpr int32_t kBuildingLookAroundRadius = 2;
inline constexpr uint32_t kBuildingLookAroundVisits = 0x19;
inline constexpr float kBuildingLookAroundDelay = 2.0f;

template<class Env> LookAroundReport LookAround(Env& env, object::Building& building)
{
    LookAroundReport report;
    if (static_cast<uint32_t>(building.m_VisitsSinceCleaned) <= kBuildingLookAroundVisits) return report;
    report.looked = true;
    object::Base* const* list = nullptr;
    const uint32_t count = env.ObjectsNear(kBuildingLookAroundRadius, list);
    for (uint32_t i = 0; i < count; ++i) {
        if (list[i]->m_type != object::ObjectType::Guest) continue;
        env.Post(BuildingMessage(building, static_cast<int32_t>(list[i]->m_id), messages::MT_SPOT_OPINION));
        ++report.guestsNotified;
    }
    env.PostDelayed(BuildingMessage(building, static_cast<int32_t>(building.m_id), messages::MT_LOOK_AROUND), kBuildingLookAroundDelay);
    return report;
}

// The price MT_IM_READY carries: the show's price for LIVEVENT (the current slot of the building's schedule, BuildingEvents), else the building's own
// price of the slot (m_Commod_Price; the slot is the one the queue gave back, not range-checked).
template<class Env> float ReadyPrice(Env& env, const object::Building& building, int32_t slot)
{
    if (NameIs(env.CommodityName(building.m_definition->m_Commodity[slot]), "LIVEVENT")) return env.EventPrice();
    return building.m_Commod_Price[slot];
}

// What one MT_COMMOD_QUALITY did.
struct CommodQualityReport {
    RentalGood rental = RentalGood::None; // the good that came back
    bool noQueue = false; // the building has no queue: the game ends with an error (Env::MissingQueue)
    bool next = false; // the next guest in the queue was told it can be served
};

// MT_COMMOD_QUALITY (0x32, sender = the guest that was served, data = the slot of the good): a rental good that came back is counted in its stock
// (SNORKEL m_NumSnorkels, else LILOS m_NumLilos, else SURFBOARD m_NumBoards; checked in this order) and the building has room for one more guest
// (m_CurCapacity - 1). A building without a queue is a fatal error of the game (the message names the sender's class and state, the building's state, name
// and open status). Otherwise the first guest of the queue, if any, takes the place: m_CurCapacity + 1 and MT_IM_READY (data = the building) to it.
// Env: `const char* CommodityName(int32_t commodityId)`, `float EventPrice()`, `void MissingQueue(int32_t senderId)`, `uint32_t QueueSize()`,
// `bool Dequeue(uint32_t& guest, int32_t& slot)` (the result is not looked at here), `void Post(const Message&)`.
template<class Env> CommodQualityReport CommodQuality(Env& env, object::Building& building, const messages::Message& message)
{
    CommodQualityReport report;
    const char* good = env.CommodityName(building.m_definition->m_Commodity[message.data]);
    if (NameIs(good, "SNORKEL")) {
        ++building.m_NumSnorkels;
        report.rental = RentalGood::Snorkel;
    } else if (NameIs(good, "LILOS")) {
        ++building.m_NumLilos;
        report.rental = RentalGood::Lilo;
    } else if (NameIs(good, "SURFBOARD")) {
        ++building.m_NumBoards;
        report.rental = RentalGood::Board;
    }
    --building.m_CurCapacity;
    if (!building.m_queue) {
        env.MissingQueue(message.sender);
        report.noQueue = true;
        return report;
    }
    if (env.QueueSize() == 0) return report;
    uint32_t guest = 0;
    int32_t slot = 0;
    env.Dequeue(guest, slot);
    ++building.m_CurCapacity;
    messages::Message ready =
            BuildingMessage(building, static_cast<int32_t>(guest), messages::MT_IM_READY, static_cast<int32_t>(building.m_id));
    ready.floatData = ReadyPrice(env, building, slot);
    env.Post(ready);
    report.next = true;
    return report;
}

// What one MT_GUEST_SERVED did.
struct GuestServedReport {
    bool boarded = false; // WHARF, BUSSTOP and COMPLAINTHUT: the queue's guests go on board (boarding queued guests)
    bool idle = false; // nobody waits: the sender is free again
    int32_t freedVehicle = 0; // vehicles (m_Vehicles) set free
    int32_t freedStaff = 0; // staff members set free
    uint32_t ready = 0; // guests of the queue told that they can be served
    uint32_t readyDelayed = 0; // ... of which the sender is not a vehicle: the message is delayed
};

inline constexpr float kReadyDelay = 3.0f;

// MT_GUEST_SERVED (0x53, sender = whoever served a guest: a vehicle or a staff member). WHARF, BUSSTOP (the definition's key) and COMPLAINTHUT (the building's
// name) hand over to the queue: boarding the queued guests onto the sender vehicle. Any other building with an empty queue marks the sender free: the vehicle
// of that id (`m_vehicleInUse` = 0; a building with vehicles looks only at them) or else the staff member (`m_staff_busy` = 0). With guests waiting the sender
// takes as many as it can serve at once, one MT_IM_READY each (data = the sender): a vehicle takes as many as its capacity, anyone else one; IM_READY goes
// at once to a vehicle and 3 s later to anyone else. A dequeue that comes back empty-handed only uses up one of the places.
// Env: `QueueSize`, `CommodityName`, `EventPrice`, `Dequeue(guest, slot)` as above, `void BoardQueuedGuests(int32_t sender)`,
// `object::Base* GetObject(int32_t id)` (the result is not checked for null), `Post`, `PostDelayed`.
template<class Env> GuestServedReport GuestServed(Env& env, object::Building& building, const messages::Message& message)
{
    GuestServedReport report;
    const BuildingBehavior behavior = building.m_definition->m_Behavior;
    if (behavior == BuildingBehavior::ShipStop || behavior == BuildingBehavior::BusStop || behavior == BuildingBehavior::ComplaintsHut) {
        env.BoardQueuedGuests(message.sender);
        report.boarded = true;
        return report;
    }
    if (env.QueueSize() == 0) {
        report.idle = true;
        if (building.m_NumVehicles != 0) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(building.m_NumVehicles); ++i) {
                if (message.sender == building.m_Vehicles[i]) {
                    building.m_vehicleInUse[i] = 0;
                    ++report.freedVehicle;
                }
            }
        } else {
            const uint32_t staff = static_cast<uint32_t>(building.m_numMalestaff + building.m_numFemalestaff);
            for (uint32_t i = 0; i < staff; ++i) {
                if (message.sender == building.m_staff[i]) {
                    building.m_staff_busy[i] = 0;
                    ++report.freedStaff;
                }
            }
        }
        return report;
    }
    const object::Base* sender = env.GetObject(message.sender);
    const bool vehicle = sender && sender->m_type == object::ObjectType::Vehicle;
    uint32_t places = 1;
    if (vehicle) places = static_cast<uint32_t>(static_cast<const object::Vehicle*>(sender)->m_definition->m_Capacity);
    for (; places != 0; --places) {
        uint32_t guest = 0;
        int32_t slot = 0;
        if (!env.Dequeue(guest, slot)) continue;
        messages::Message ready = BuildingMessage(building, static_cast<int32_t>(guest), messages::MT_IM_READY, message.sender);
        ready.floatData = ReadyPrice(env, building, slot);
        if (vehicle) {
            env.Post(ready);
        } else {
            env.PostDelayed(ready, kReadyDelay);
            ++report.readyDelayed;
        }
        ++report.ready;
    }
    return report;
}

// ---- ReceiveMessage: MT_IM_READY ---------------------------------------------------------------------------------------
//
// MT_IM_READY (0x1e) as the building gets it: a guest has reached the place where it is served (sender = the guest, flags = the slot of the good it
// wants, data = the id of whoever serves it: the building itself or a member of its staff / a vehicle). The building books the sale and sends the guest
// (or the server) on. The words after data of the records built here are zero.

// How the sale was passed on (the branch counters of the handler).
enum class ReadyRoute : uint8_t {
    SelfTables, // the building serves itself and has tables: the guest is seated at the first table that takes it
    SelfLoungers, // ... and has loungers: the first free one is claimed for the guest
    SelfBeer, // ... sells alcohol: MT_SERVED_COMMOD 4 s later, with the beer's effect and price
    SelfEvent, // ... a show: the guest joins the audience and is served at once
    SelfNow, // ... swimming and the fun slide: served at once
    SelfDelayed, // ... anything else: served 4 s later
    OtherTables, // somebody else serves, the building has tables: the table of that vehicle seats the guest
    OtherPlain, // ... a plain good: the server is told to serve the guest
    OtherBeer, // ... with a beer chosen: the server is also told the beer's effect and price
};

struct ImReadyReport {
    bool live = false; // the good is a live event: price and cost come from the schedule
    bool foodIll = false; // the guest ate or drank in polluted water
    bool beerDrawn = false; // the 20th round of beer: the effect was drawn instead of read
    ReadyRoute route = ReadyRoute::SelfDelayed;
    int32_t seated = 0; // SelfTables / OtherTables: how many SeatGuest calls were made
};

inline constexpr float kServeDelay = 4.0f; // the object-served good and the beer reach the guest this much later
inline constexpr int32_t kBeerDrawEvery = 0x14; // every 20th beer served by any building draws its effect
inline constexpr uint32_t kBeerDrawRating = 0x32; // resort rating above this: three effects to draw from (0, 2, 4), else two (1, 3)
inline constexpr float kPollutionPerRepairStep = 0.33f; // how much of the pollution a half-repaired water cleaner stops

// float + float in double (53-bit) precision and stored as a float; the rounding is pinned by tests.
inline float AddRounded(float a, float b)
{
    return static_cast<float>(static_cast<double>(a) + static_cast<double>(b));
}

inline messages::Message ReadyMessage(
        const object::Building& building, int32_t receiver, int32_t type, int32_t flags, int32_t data, float amount = 0.0f)
{
    messages::Message message = BuildingMessage(building, receiver, type, data);
    message.flags = flags;
    message.floatData = amount;
    return message;
}

// ---- ReceiveMessage: MT_REQUEST_COMMOD -------------------------------------------------------------------------------

struct RequestCommodReport {
    RentalGood rental = RentalGood::None;
    bool lounger = false;
    bool live = false;
    bool available = true;
};

// data is the commodity slot (unlike IM_READY's flags). Even a refused request counts as a visit. Rental stock is reserved before the guest accepts
// the price; a free lounger is only checked here and is claimed by IM_READY later. Env is the same as ImReady: GameWorld, CommodityName, GetObject,
// EventPrice and Post. Every lounger is scanned and the count is compared as signed.
template<class Env> RequestCommodReport RequestCommod(Env& env, object::Building& building, const messages::Message& message)
{
    ++building.m_NumVisitsToday;
    if (env.GameWorld().m_allowLitter != 0) ++building.m_VisitsSinceCleaned;

    RequestCommodReport report;
    const char* key = env.CommodityName(building.m_definition->m_Commodity[message.data]);
    int32_t* stock = nullptr;
    if (NameIs(key, "SNORKEL")) {
        report.rental = RentalGood::Snorkel;
        stock = &building.m_NumSnorkels;
    } else if (NameIs(key, "LILOS")) {
        report.rental = RentalGood::Lilo;
        stock = &building.m_NumLilos;
    } else if (NameIs(key, "SURFBOARD")) {
        report.rental = RentalGood::Board;
        stock = &building.m_NumBoards;
    } else if (NameIs(key, "SUNLNGER")) {
        report.lounger = true;
        report.available = false;
        for (int32_t i = 0; i < static_cast<int32_t>(building.m_numLoungers); ++i) {
            const object::Base* lounger = env.GetObject(building.m_loungers[i]);
            if (lounger && lounger->m_state == 0) // every listed lounger should exist; a missing one is skipped
                report.available = true;
        }
    }
    if (stock) {
        report.available = *stock != 0; // equality, not a positive-stock check: negative stocks are decremented too
        if (report.available) --*stock;
    }

    if (!report.available) {
        env.Post(ReadyMessage(building, message.sender, messages::MT_NO_STOCK, 0, 0));
    } else {
        report.live = NameIs(key, "LIVEVENT");
        const float price = report.live ? env.EventPrice() : building.m_Commod_Price[message.data];
        env.Post(ReadyMessage(building, message.sender, messages::MT_COMMOD_PRICE, 0, 0, price));
    }
    return report;
}

// The effect a served beer has on the guest and its price (BEER_<key> ALCSTR times the building's m_BeerStrength). The effect is the beer's own, except
// that a counter shared by all buildings makes every 20th beer draw: above kBeerDrawRating by `rand() % 3` (0 FIGHT, 2 VANDAL, 4 SAD), else by `rand() % 2`
// (1 FLIRT, 3 HAPPY). The draw ends the round and the counter starts again. The `family` stands in for a remainder that no draw gives.
// Env: `const BeerDef& Beer(int32_t id)`, `int32_t& BeerCounter()`, `uint32_t ResortRating()`, `int Rand()`.
template<class Env> float BeerRound(Env& env, const object::Building& building, int32_t family, int32_t& effect, bool& drawn)
{
    const tables::BeerDef& beer = env.Beer(building.m_BeerType);
    const float price = static_cast<float>(static_cast<double>(beer.m_AlcStr) * static_cast<double>(building.m_BeerStrength));
    int32_t& counter = env.BeerCounter();
    ++counter;
    if (counter != kBeerDrawEvery) {
        effect = beer.m_Effect;
        return price;
    }
    drawn = true;
    effect = family;
    if (env.ResortRating() > kBeerDrawRating) {
        const int remainder = env.Rand() % 3;
        if (remainder == 0)
            effect = 0;
        else if (remainder == 1)
            effect = 2;
        else if (remainder == 2)
            effect = 4;
    } else {
        const int remainder = env.Rand() % 2;
        if (remainder == 0)
            effect = 1;
        else if (remainder == 1)
            effect = 3;
    }
    counter = 0;
    return price;
}

// MT_IM_READY. First the books (the numbers of the good's slot; a live event takes price and cost from the show that is on, BuildingEvents slot
// m_CurrentSlot): the building's income and cost of the day, the world's income (and the one of the building's category: 0 hotels, with HOTEL and ACCOMBLK
// as accommodation and everything else of the category as retail, 1 food as retail, 2 leisure as attractions, others only the total), the world's cost
// and stock cost of the day, and the player's cash (cash + price - cost). A drink or a meal in polluted water makes the guest ill, less so when a water
// cleaner (WATERCLN) works: the first one of the list, if open, stops 0.33 of the pollution for each repair step it is in; ICESTALL, BARBEQUE and FASTFOOD
// are exempt. Then the sale is passed on, by who serves:
//   the building itself (data == its id): tables > loungers > beer > event > swimming / fun slide > the rest;
//   somebody else: tables (the table of the same index as that vehicle in m_Vehicles seats the guest) > beer type chosen > nothing chosen.
// The answer carries no meaning here; the handler returns 0.
// Env: `World& GameWorld()`, `CommodityName`, `EventPrice`, `float EventCost()`, `int32_t FamilyOf(commodity)`, `int32_t FamilyId(key)`, `int32_t CommodityId(key)`,
// `Entity* const* FindBuildings(BuildingBehavior, count&)`,`void AddFoodIll(guest, amount)`, `bool SeatGuest(table, guest)`, `Entity* GetObject(id)`,
// `void AddEventGuest(guest)`, `Post`, `PostDelayed` and what BeerRound uses.
template<class Env> ImReadyReport ImReady(Env& env, object::Building& building, const messages::Message& message)
{
    ImReadyReport report;
    auto& world = env.GameWorld();
    const tables::BuildingDef& def = *building.m_definition;
    const int32_t slot = message.flags;
    const int32_t commodity = def.m_Commodity[slot];
    const int32_t buildingId = static_cast<int32_t>(building.m_id);
    const int32_t guest = message.sender;

    report.live = NameIs(env.CommodityName(commodity), "LIVEVENT");
    const float price = report.live ? env.EventPrice() : building.m_Commod_Price[slot];
    building.m_incomeToday = AddRounded(building.m_incomeToday, price);
    const float cost = report.live ? env.EventCost() : building.m_Commod_Cost[slot];
    building.m_costToday = AddRounded(building.m_costToday, cost);

    float* category = nullptr;
    switch (def.m_Category) {
    case 0:
        category = building.m_definition->m_Behavior == BuildingBehavior::Hotel ||
                        building.m_definition->m_Behavior == BuildingBehavior::AccomBlock
                ? &world.m_accomIncomeToday
                : &world.m_retailIncomeToday;
        break;
    case 1: category = &world.m_retailIncomeToday; break;
    case 2: category = &world.m_attractIncomeToday; break;
    default: break;
    }
    world.m_incomeToday = AddRounded(world.m_incomeToday, price);
    if (category) *category = AddRounded(*category, price);
    world.m_costsToday = AddRounded(world.m_costsToday, cost);
    world.m_stockCostToday = AddRounded(world.m_stockCostToday, cost);
    world.m_player->m_cash =
            static_cast<float>(static_cast<double>(world.m_player->m_cash) + static_cast<double>(price) - static_cast<double>(cost));

    const int32_t family = env.FamilyOf(commodity);
    if ((family == env.FamilyId("MEALS") || family == env.FamilyId("SNACKS") || family == env.FamilyId("ALCOHOL") ||
                family == env.FamilyId("SOFTDRNK")) &&
            world.m_waterContam > 0.0f && building.m_definition->m_Behavior != BuildingBehavior::IceStall &&
            building.m_definition->m_Behavior != BuildingBehavior::FastFood) {
        uint32_t cleaners = 0;
        object::Base* const* list = env.FindBuildings(BuildingBehavior::WaterCleaner, cleaners);
        float amount = world.m_waterContam;
        if (cleaners != 0) {
            const auto* cleaner = static_cast<const object::Building*>(list[0]);
            if (cleaner->m_OpenStatus == 0) {
                const int32_t steps = static_cast<int32_t>(3u - cleaner->m_RepairState);
                amount = static_cast<float>((1.0 - static_cast<double>(steps) * static_cast<double>(kPollutionPerRepairStep)) *
                        static_cast<double>(world.m_waterContam));
            }
        }
        env.AddFoodIll(guest, amount);
        report.foodIll = true;
    }

    if (message.data == buildingId) {
        if (building.m_numTables != 0) {
            report.route = ReadyRoute::SelfTables;
            for (int32_t i = 0; i < static_cast<int32_t>(building.m_numTables); ++i) {
                ++report.seated;
                if (env.SeatGuest(building.m_Tables[i], guest)) break;
            }
            return report;
        }
        if (building.m_numLoungers != 0) {
            report.route = ReadyRoute::SelfLoungers;
            for (int32_t i = 0; i < static_cast<int32_t>(building.m_numLoungers); ++i) {
                const object::Base* lounger = env.GetObject(building.m_loungers[i]);
                if (!lounger || lounger->m_state != 0) // a building's loungers exist; a missing one counts as busy
                    continue;
                // The guest is served, sent to the lounger (MT_STATECHANGE 4 from itself) and told where to walk (MT_PAVEMENT2OBJECT).
                env.Post(ReadyMessage(building, guest, messages::MT_SERVED_COMMOD, 0, 0));
                messages::Message lie = ReadyMessage(building, building.m_loungers[i], messages::MT_STATECHANGE, 0, 4);
                lie.sender = guest;
                env.Post(lie);
                env.Post(ReadyMessage(building, guest, messages::MT_PAVEMENT2OBJECT, 0, building.m_loungers[i]));
                break;
            }
            return report;
        }
        const messages::Message served = ReadyMessage(building, guest, messages::MT_SERVED_COMMOD, 0, 0);
        if (family == env.FamilyId("ALCOHOL")) {
            report.route = ReadyRoute::SelfBeer;
            int32_t effect = 0;
            const float amount = BeerRound(env, building, family, effect, report.beerDrawn);
            env.PostDelayed(ReadyMessage(building, guest, messages::MT_SERVED_COMMOD, effect, buildingId, amount), kServeDelay);
        } else if (commodity == env.CommodityId("LIVEVENT")) {
            report.route = ReadyRoute::SelfEvent;
            env.AddEventGuest(guest);
            env.Post(served);
        } else if (commodity == env.CommodityId("SWIMMING")) {
            report.route = ReadyRoute::SelfNow;
            env.Post(served);
        } else if (commodity == env.CommodityId("FUNSLIDE")) {
            report.route = ReadyRoute::SelfNow;
            env.Post(served);
        } else {
            report.route = ReadyRoute::SelfDelayed;
            env.PostDelayed(served, kServeDelay);
        }
        return report;
    }

    if (building.m_numTables != 0) {
        report.route = ReadyRoute::OtherTables;
        for (uint32_t i = 0; i < static_cast<uint32_t>(building.m_NumVehicles); ++i) {
            if (message.data == building.m_Vehicles[i]) {
                env.SeatGuest(building.m_Tables[i], guest);
                ++report.seated;
            }
        }
        return report;
    }
    if (building.m_BeerType == -1) {
        report.route = ReadyRoute::OtherPlain;
        env.Post(ReadyMessage(building, message.data, messages::MT_SERVE_GUEST, 0, guest));
        return report;
    }
    report.route = ReadyRoute::OtherBeer;
    int32_t effect = 0;
    const float amount = BeerRound(env, building, family, effect, report.beerDrawn);
    env.Post(ReadyMessage(building, message.data, messages::MT_SERVE_GUEST, effect, guest, amount));
    return report;
}

// ---- ReceiveMessage: the guest's answer to the price (PRICE_OK, PRICE_NOT_OK) and the end of a job on the base plate -----------------------------
//
// REQUEST_COMMOD quoted the price (and, for the rental goods, took one piece of the stock); the guest answers with MT_PRICE_OK or MT_PRICE_NOT_OK.

// How PRICE_OK found a server for the guest (the branch counters of the handler).
enum class PriceOkRoute : uint8_t {
    Vehicle, // a vehicle that is free takes the guest (the first one of m_Vehicles)
    Staff, // ... a staff member that is free (the first one that is not busy)
    SelfServed, // a building without vehicles and staff serves itself: there is room (a place counted in m_CurCapacity)
    Queued, // nobody is free: the guest joins the queue
};

struct PriceOkReport {
    PriceOkRoute route = PriceOkRoute::Queued;
};

// MT_PRICE_OK (0x46, sender = the guest that accepts the price, data = the slot of its good): the guest is told with MT_IM_READY (flags 0, data = the
// server) to go to whoever serves it, and the server is marked busy. A building with vehicles looks only at them (`m_vehicleInUse`), else a building with staff
// only at the staff (`m_staff_busy`, ids in `m_staff`); the first free one gets the guest. With none free the guest joins the queue with its slot
// (the queue moves the guest into the requested place). A building with neither serves itself while it has room: the guests in service
// (`m_CurCapacity`) are fewer than the definition's `m_GuestCapacity` or one of the rental stocks is not empty, and, with loungers, fewer than their number
// (the compares are unsigned, a zero count of loungers means no limit); the guest is counted in, IM_READY has the building as its server.
// floatData of IM_READY is zero. The answer byte carries no meaning.
// Env: `void Post(const Message&)` and `void Enqueue(int32_t guestId, bool moveGuest, int32_t slot)`.
template<class Env> PriceOkReport PriceOk(Env& env, object::Building& building, const messages::Message& message)
{
    PriceOkReport report;
    const int32_t guest = message.sender;
    if (building.m_NumVehicles != 0) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(building.m_NumVehicles); ++i) {
            if (building.m_vehicleInUse[i] != 0) continue;
            env.Post(ReadyMessage(building, guest, messages::MT_IM_READY, 0, building.m_Vehicles[i]));
            building.m_vehicleInUse[i] = 1;
            report.route = PriceOkRoute::Vehicle;
            return report;
        }
    } else if (building.m_numMalestaff + building.m_numFemalestaff != 0) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(building.m_numMalestaff + building.m_numFemalestaff); ++i) {
            if (building.m_staff_busy[i] != 0) continue;
            env.Post(ReadyMessage(building, guest, messages::MT_IM_READY, 0, building.m_staff[i]));
            building.m_staff_busy[i] = 1;
            report.route = PriceOkRoute::Staff;
            return report;
        }
    } else {
        const uint32_t capacity = static_cast<uint32_t>(building.m_CurCapacity);
        const bool room = capacity < static_cast<uint32_t>(building.m_definition->m_GuestCapacity) || building.m_NumLilos != 0 ||
                building.m_NumSnorkels != 0 || building.m_NumBoards != 0;
        if (room && (building.m_numLoungers == 0 || capacity < building.m_numLoungers)) {
            building.m_CurCapacity = static_cast<int32_t>(capacity + 1);
            env.Post(ReadyMessage(building, guest, messages::MT_IM_READY, 0, static_cast<int32_t>(building.m_id)));
            report.route = PriceOkRoute::SelfServed;
            return report;
        }
    }
    env.Enqueue(guest, true, message.data);
    return report;
}

struct PriceNotOkReport {
    RentalGood rental = RentalGood::None; // the good whose stock the refused guest gives back
    bool released = false; // the server was given up (data != 0)
    bool queueEmpty = false; // ... and nobody waits: the place and the server are free
    int32_t freedVehicle = 0;
    int32_t freedStaff = 0;
    bool dequeued = false; // somebody waits: the first guest of the queue was taken
    bool swallowed = false; // ... a rental good or a lounger: that guest is told nothing (see below)
    bool ready = false; // ... any other good: the guest is told with IM_READY, the server is its
};

// MT_PRICE_NOT_OK (0x47, sender = the guest that refuses the price, flags = the slot of its good, data = the server it was given, 0 for none): the rental
// good comes back to the stock (SNORKEL m_NumSnorkels, else LILOS m_NumLilos, else SURFBOARD m_NumBoards). Without a server (data == 0) that is all. Else,
// with an empty queue, the guest's place is free again (m_CurCapacity - 1) and so is the server: every vehicle of m_Vehicles with that id (a building with
// vehicles) or else every staff member with that id (m_staff_busy = 0). With guests waiting the first one is taken out of the queue (the server stays
// with it) and told with MT_IM_READY (data = the server, floatData = the price of its good as ReadyPrice) - except that the rental goods and
// the loungers get only the bookkeeping: a rental good is taken out of the stock again when there is one (stock != 0, not a positive-stock check) and a
// SUNLNGER only looks at its loungers (no effect); in both the guest hears nothing (the compare with LIVEVENT in those branches can never be
// true). A dequeue that gives nothing ends the message. The loop over the loungers is not run here: it only looked objects up.
// Env: `CommodityName`, `EventPrice`, `QueueSize`, `Dequeue(guest, slot)` and `Post`.
template<class Env> PriceNotOkReport PriceNotOk(Env& env, object::Building& building, const messages::Message& message)
{
    PriceNotOkReport report;
    const char* good = env.CommodityName(building.m_definition->m_Commodity[message.flags]);
    if (NameIs(good, "SNORKEL")) {
        ++building.m_NumSnorkels;
        report.rental = RentalGood::Snorkel;
    } else if (NameIs(good, "LILOS")) {
        ++building.m_NumLilos;
        report.rental = RentalGood::Lilo;
    } else if (NameIs(good, "SURFBOARD")) {
        ++building.m_NumBoards;
        report.rental = RentalGood::Board;
    }
    if (message.data == 0) return report;
    report.released = true;

    if (env.QueueSize() == 0) {
        report.queueEmpty = true;
        --building.m_CurCapacity;
        if (building.m_NumVehicles != 0) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(building.m_NumVehicles); ++i) {
                if (message.data == building.m_Vehicles[i]) {
                    building.m_vehicleInUse[i] = 0;
                    ++report.freedVehicle;
                }
            }
        } else if (building.m_numMalestaff + building.m_numFemalestaff != 0) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(building.m_numMalestaff + building.m_numFemalestaff); ++i) {
                if (message.data == building.m_staff[i]) {
                    building.m_staff_busy[i] = 0;
                    ++report.freedStaff;
                }
            }
        }
        return report;
    }

    uint32_t next = 0;
    int32_t slot = 0;
    if (!env.Dequeue(next, slot)) return report;
    report.dequeued = true;
    const char* wanted = env.CommodityName(building.m_definition->m_Commodity[slot]);
    int32_t* stock = nullptr;
    if (NameIs(wanted, "SNORKEL"))
        stock = &building.m_NumSnorkels;
    else if (NameIs(wanted, "LILOS"))
        stock = &building.m_NumLilos;
    else if (NameIs(wanted, "SURFBOARD"))
        stock = &building.m_NumBoards;
    if (stock) {
        if (*stock != 0) --*stock;
        report.swallowed = true;
        return report;
    }
    if (NameIs(wanted, "SUNLNGER")) {
        report.swallowed = true;
        return report;
    }
    messages::Message ready = ReadyMessage(building, static_cast<int32_t>(next), messages::MT_IM_READY, 0, message.data);
    ready.floatData = ReadyPrice(env, building, slot);
    env.Post(ready);
    report.ready = true;
    return report;
}

// MT_BUILDERS_LEFT_BASEPLATE (0x62, to itself from the construction manager, data = which job ended: MT_CONSTRUCT (0x28) for a built building, else a
// demolished one, ConstructionJobs): the building is brought up (built: the open state is decided and the construction manager's job is finished, completing construction)
// and tells itself to change state: MT_STATECHANGE with state 0 (working) after a build, 3 (gone) after a demolition. Env: `UpdateOpenState`, `FinishBuild`, `Post`.
template<class Env> bool BuildersLeftBaseplate(Env& env, object::Building& building, const messages::Message& message)
{
    const bool built = message.data == static_cast<int32_t>(messages::MT_CONSTRUCT);
    int32_t state = 3;
    if (built) {
        env.UpdateOpenState();
        env.FinishBuild();
        state = 0;
    }
    const int32_t buildingId = static_cast<int32_t>(building.m_id);
    env.Post(BuildingMessage(building, buildingId, messages::MT_STATECHANGE, state));
    return built;
}

} // namespace obl
