#pragma once

#include "game/objects/building_breakdown.h"
#include "game/objects/building_update_tail.h"
#include "game/messages/message_state.h"
#include "game/objects/object_building.h"
#include "game/objects/object_table.h"

#include <cstdint>

// The building's Update as one function over an Env (docs/architecture/objects.md): the dispatcher on `m_state` and the two
// states that were not part of the working tail (building_update_tail.h) - STATE_STARTUP (1) and the demolition (2). No game memory here: the
// glue in src/game/objects/building_methods.cpp implements the Env with the calls to the game.
namespace obl {

// State numbers of Building::m_state the dispatcher tells apart; 3 and everything above 4 only run the sound flag.
inline constexpr int32_t kBuildingStateNull = 0;
inline constexpr int32_t kBuildingStateStartup =
        1; // STATE_STARTUP: the building site, OnExitState brings up the queue, the show and the power
inline constexpr int32_t kBuildingStateDemolish = 2; // OnEnterState starts the demolition job
inline constexpr int32_t kBuildingStateOccupied = 4;

// The message the demolition sends to the loungers: MT_STATECHANGE (3) with the new state 3 in the data word.
inline constexpr int32_t kLoungerDemolishState = 3;

// No seat of the table has an occupier. The seats are `m_definition->m_NumChairs` records.
inline bool AreChairsEmpty(const object::Furniture& table)
{
    const uint32_t chairs = table.m_definition->m_NumChairs;
    for (uint32_t i = 0; i < chairs; ++i) {
        if (table.m_chairs[i].m_ChairOccupier != -1) return false;
    }
    return true;
}

// What one UpdateDemolishing call did.
struct DemolishReport {
    bool waiting = false; // the demolition had not started: the building is closed and the hoarding ticked
    bool emptied = false; // nobody left inside, so the parts were marked for removal and the demolition started
    bool eventCancelled = false;
};

// States 2: while the demolition has not started the building waits to be empty. Every call closes it (`m_OpenStatus` = 1),
// ticks the hoarding and looks whether all vehicles are back, all seats of the tables are free and every lounger is idle (state 0); with
// no capacity left (`m_CurCapacity` < 1, signed), no guests inside (`m_NumGuests` == 0) and all of that true it removes its vehicles, tables,
// loungers (told MT_STATECHANGE to state 3) and service staff (class 6 only: mechanics, builders and cleaners stay) and starts the demolition.
// A missing object of the lists (a stale id) is skipped.
// The Env gives, in the order the code calls it (tests pin the order):
//   bool IsStarted()                   whether the building construction job has started
//   int ActiveEventId()                the current event id, queried only for a building with a schedule
//   void CancelCurrentEvent()          cancels the scheduled event
//   void TickHoarding()                advances the building construction hoarding
//   object::Base* GetObject(id)  the object of an id, null when there is none
//   void Post(const messages::Message&)  posts to the global message handler
//   void SetStarted()                  marks the building construction job as started
template<class Env> DemolishReport UpdateDemolishing(Env& env, object::Building& building)
{
    DemolishReport report;
    if (env.IsStarted()) return report;
    report.waiting = true;
    if (building.m_events && env.ActiveEventId() != -1) {
        env.CancelCurrentEvent();
        report.eventCancelled = true;
    }
    building.m_OpenStatus = 1;
    env.TickHoarding();

    bool clear = true;
    const uint8_t* inUse = building.m_vehicleInUse;
    for (uint32_t i = 0; i < static_cast<uint32_t>(building.m_NumVehicles); ++i) {
        if (inUse[i] == 1) // exactly 1, unlike the hire buildings' test for a free slot
            clear = false;
    }
    for (int32_t i = 0; i < static_cast<int32_t>(building.m_numTables); ++i) {
        const auto* table = static_cast<const object::Furniture*>(env.GetObject(building.m_Tables[i]));
        if (table && !AreChairsEmpty(*table)) clear = false;
    }
    for (int32_t i = 0; i < static_cast<int32_t>(building.m_numLoungers); ++i) {
        const object::Base* lounger = env.GetObject(building.m_loungers[i]);
        if (lounger && lounger->m_state != 0) clear = false;
    }
    if (building.m_CurCapacity >= 1 || building.m_NumGuests != 0 || !clear) return report;

    // The parts go: the object is marked (m_waitingForDeath) and the building forgets it one by one, or all at once for the loungers.
    auto mark = [&](int32_t id) {
        if (object::Base* object = env.GetObject(id)) object->m_waitingForDeath = 1;
    };
    for (int32_t n = static_cast<int32_t>(building.m_NumVehicles), i = 0; n > 0; --n, ++i) {
        mark(building.m_Vehicles[i]);
        --building.m_NumVehicles;
    }
    for (int32_t n = static_cast<int32_t>(building.m_numTables), i = 0; n > 0; --n, ++i) {
        mark(building.m_Tables[i]);
        --building.m_numTables;
    }
    const int32_t loungers = static_cast<int32_t>(building.m_numLoungers);
    for (int32_t i = 0; i < loungers; ++i) {
        messages::Message message{};
        message.sender = static_cast<int32_t>(building.m_id);
        message.receiver = building.m_loungers[i];
        message.type = messages::MT_STATECHANGE;
        message.data = kLoungerDemolishState;
        env.Post(message);
        mark(building.m_loungers[i]);
    }
    building.m_numLoungers = 0;
    auto markServiceStaff = [&](const int32_t* ids, int32_t count) {
        for (int32_t i = 0; i < count; ++i) {
            object::Base* object = env.GetObject(ids[i]);
            if (object && object->m_type == object::ObjectType::ServiceStaff) object->m_waitingForDeath = 1;
        }
    };
    markServiceStaff(building.m_staff, building.m_numFemalestaff + building.m_numMalestaff);
    markServiceStaff(building.m_NonServeStaff, building.m_NumNonServestaff);
    env.SetStarted();
    report.emptied = true;
    return report;
}

// What one UpdateBuilding call did, for the branch counters and the unit tests.
struct UpdateReport {
    int32_t state = 0;
    WearReport wear;
    TailReport tail;
    DemolishReport demolish;
};

// The whole of the building's Update after the static object's Update succeeded (the caller ends the game with a FatalError when it did not):
//   states 0 and 4  BeginWorkingUpdate, UpdateWear, UpdateWorkingTail;
//   state 1         updating the construction hoarding (Env::TickHoarding);
//   state 2         UpdateDemolishing;
//   other states    nothing (the switch compares the state as unsigned, 0..4);
// then the sound flag in every case. `day`, `dayTime` and `breakdownsAllowed` are the world's game day, day time and breakdowns flag.
template<class Env> UpdateReport UpdateBuilding(Env& env, object::Building& building, int32_t day, float dayTime, bool breakdownsAllowed)
{
    UpdateReport report;
    report.state = building.m_state;
    switch (static_cast<uint32_t>(building.m_state)) {
    case kBuildingStateNull:
    case kBuildingStateOccupied:
        BeginWorkingUpdate(env, building);
        report.wear = UpdateWear(env, building, day, dayTime, breakdownsAllowed);
        report.tail = UpdateWorkingTail(env, building);
        break;
    case kBuildingStateStartup: env.TickHoarding(); break;
    case kBuildingStateDemolish: report.demolish = UpdateDemolishing(env, building); break;
    default: break;
    }
    UpdateSoundFlag(env, building);
    return report;
}

} // namespace obl
