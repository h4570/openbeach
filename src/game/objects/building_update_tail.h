#pragma once

#include "game/tables/building_behavior.h"
#include "game/objects/object_building.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>

// The rest of the building's Update in STATE_NULL (0) and STATE_OCCUPIED (4) after the wear step, and the sound flag every state
// ends with (docs/architecture/objects.md). No game memory here: the glue in src/game/objects/building_methods.cpp implements the Env with
// the calls to the game, this file holds the order and the decisions. Like UpdateWear the functions write the building's own fields before
// every call that follows them, so a helper (or the cascade of the messages it sends) already sees the updated building.
namespace obl {

inline constexpr char kFliesSound[] = "flies";
inline constexpr char kJetskiIdleSound[] = "newjetskiidle";
inline constexpr float kLoopSoundVolume = 0.6f; // both loop starts

// A toilet that has seen more than this many visits since the last clean-up asks for a cleaner (unsigned compare) ...
inline constexpr uint32_t kToiletCleanVisits = 0x19;
// ... and from this many visits on the flies buzz around it.
inline constexpr uint32_t kToiletFliesVisits = 0x14;

// Equal to zero, and also unordered: a NaN counts as zero here. The construction queries return a float.
inline bool IsZeroOrNaN(float value)
{
    return !(value < 0.0f || value > 0.0f);
}

// What one UpdateWorkingTail call did (for the branch counters and the unit tests; the building fields carry the rest).
enum class LoopCall : uint8_t { None, Start, Stop };

struct TailReport {
    bool events = false; // Updating the event schedule was called (the building has a schedule)
    bool cleaning = false; // the toilet asked for a cleaner (message 0x54 to itself, a CleaningManager job, SetMoving)
    LoopCall flies = LoopCall::None; // toilet: the "flies" loop started (> 20 visits) or the building's loop stopped
    bool buildingSound = false; // a building whose m_soundPlaying is set re-asked for its scripted sound
    LoopCall hire = LoopCall::None; // jet ski / speedboat hire: "newjetskiidle" started (open, a free vehicle) or stopped
};

// ---- the head of states 0 and 4 --------------------------------------------------------------------------------------------

// A selected building (`m_SelectLevel` == 1) closes the dialog it provides (closing the provider dialog, the result is
// ignored) and the day's profit is recomputed. Env: void CloseDialog().
template<class Env> void BeginWorkingUpdate(Env& env, object::Building& building)
{
    if (building.m_SelectLevel == 1) env.CloseDialog();
    building.m_profitToday = building.m_incomeToday - building.m_costToday;
}

// ---- the tail ------------------------------------------------------------------------------------------------------------

// The Env gives what the tail asks of the game, in call order (tests pin it):
//   void UpdateEvents()                 the update of m_events, asked only when the building has a schedule
//   void UpdateOpenState()              UpdateOpenState
//   float IsBuilding()                  the construction manager's build progress of id
//   float IsDemolishing()               the construction manager's demolition progress of id
//   bool IsCleanerClaimed()             IsCleanerClaimed
//   void SendCleaningMessage()          the building sends itself a message of type 0x54 (id, id, 0x54, 0, 0)
//   void AddCleaningJob()               cleaning::cleaning_facade::AddJob of the world's cleaning manager
//   void SetMoving()                    SetMoving(this, true)
// void LoopSoundStart(const char*) the looping sound set: name, id, volume kLoopSoundVolume, loop on
//   void LoopSoundStop()                the same set: stop the loop of this building's id
// void BuildingSoundStart ScriptedSound: start the building's sound by its name (the game's own table)
// Only the toilet (`LOO`) is cleaned; the toilet, the jet ski hire and the speedboat hire have loops of their own.
template<class Env> TailReport UpdateWorkingTail(Env& env, object::Building& building)
{
    TailReport report;
    if (building.m_events) {
        env.UpdateEvents();
        report.events = true;
    }
    env.UpdateOpenState();

    // The toilet's cleaning call; the cheap tests first (the name last).
    if (!building.m_calledCleaning && static_cast<uint32_t>(building.m_VisitsSinceCleaned) > kToiletCleanVisits &&
            IsZeroOrNaN(env.IsBuilding()) && IsZeroOrNaN(env.IsDemolishing()) && !env.IsCleanerClaimed() &&
            building.m_definition->m_Behavior == BuildingBehavior::Toilet) {
        env.SendCleaningMessage();
        env.AddCleaningJob();
        env.SetMoving();
        report.cleaning = true;
    }

    // The sound of the toilet (flies near a dirty one) or of the other buildings that keep a scripted sound going.
    if (building.m_definition->m_Behavior == BuildingBehavior::Toilet) {
        if (static_cast<uint32_t>(building.m_VisitsSinceCleaned) > kToiletFliesVisits) {
            env.LoopSoundStart(kFliesSound);
            report.flies = LoopCall::Start;
        } else {
            env.LoopSoundStop();
            report.flies = LoopCall::Stop;
        }
    } else if (building.m_soundPlaying) {
        env.BuildingSoundStart();
        report.buildingSound = true;
    }

    // The hire buildings idle their engines while they are open and have a vehicle on the quay (a free slot of m_vehicleInUse).
    const BuildingBehavior behavior = building.m_definition->m_Behavior;
    if (behavior == BuildingBehavior::JetskiHire || behavior == BuildingBehavior::SpeedboatHire) {
        bool vehicleFree = false;
        if (building.m_OpenStatus != 1) {
            const uint8_t* inUse = building.m_vehicleInUse; // m_NumVehicles flags; the data keeps the count within the array
            for (uint32_t i = 0; i < static_cast<uint32_t>(building.m_NumVehicles); ++i) {
                if (inUse[i] == 0) {
                    vehicleFree = true;
                    break;
                }
            }
        }
        if (vehicleFree) {
            env.LoopSoundStart(kJetskiIdleSound);
            report.hire = LoopCall::Start;
        } else {
            env.LoopSoundStop();
            report.hire = LoopCall::Stop;
        }
    }
    return report;
}

// ---- the end of every state ---------------------------------------------------------------------------------------------------

// the scripted sound follows the opening: a building that is closed (`m_OpenStatus` == 1) stops its sound, one that is open (0) and
// silent starts it, and `m_soundPlaying` is set after the start and cleared after the stop. Any other status leaves both as they are.
// Env: void BuildingSoundStop() (the loop set's stop for the building's id), void BuildingSoundStart().
template<class Env> void UpdateSoundFlag(Env& env, object::Building& building)
{
    if (building.m_soundPlaying) {
        if (building.m_OpenStatus == 1) {
            env.BuildingSoundStop();
            building.m_soundPlaying = 0;
        }
    } else if (building.m_OpenStatus == 0) {
        env.BuildingSoundStart();
        building.m_soundPlaying = 1;
    }
}

} // namespace obl
