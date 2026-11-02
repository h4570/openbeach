#pragma once

#include "game/events/event_state.h"
#include "game/weather/weather_state.h"

#include "game/feedback/feedback_state.h"
#include "game/ferry/ferry_state.h"
#include "game/repair/repair_state.h"
#include "game/cleaning/cleaning_state.h"
#include "game/construction/construction_state.h"
#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/world/world_state.h"
#include "game/objects/object_manager_state.h"
#include "game/world/world_snapshot.h"

#include <cstdint>

// The world snapshot (schema 2, docs/testing/world-hashes.md) emitted from the model instead of through the save path. The records,
// their order, names, type tags and bytes are pinned by the reference hashes. Each
// emitter here writes them from the state directly, so the save format can change without touching the hash, and a module that moves
// to the native model only changes where its emitter reads from (the replays and golden hashes check it).
// Records that Normalize erases are not emitted; constants the binary save wrote (`sizeof`) keep their values.
namespace obl::world_snapshot {

void EmitWorldFields(WorldSnapshot& s, const world::World& world, uint32_t scriptState);
void EmitGrid(WorldSnapshot& s, const map::Grid& grid);
void EmitPath(WorldSnapshot& s);
void EmitObjects(WorldSnapshot& s, const object::Manager& objects); // world_snapshot_objects.cpp
void EmitPlayer(WorldSnapshot& s, const player::Player& player);
void EmitFerry(WorldSnapshot& s, const ferry::Manager& ferry);
void EmitEvents(WorldSnapshot& s, const disaster::Manager& events);
void EmitRepair(WorldSnapshot& s, const repair::Manager& repair);
void EmitFeedback(WorldSnapshot& s, const feedback::Manager& feedback);
void EmitMessages(WorldSnapshot& s, const messages::Handler& handler);
void EmitCleaning(WorldSnapshot& s, const cleaning::Manager& cleaning);
void EmitConstruction(WorldSnapshot& s, const construction::Manager& construction);
void EmitWeather(WorldSnapshot& s, const weather::Weather& weather);

// The whole world in the snapshot's record order. The caller runs Normalize afterwards, which leaves the emitted records as they are
// except for the conditional cases documented there.
inline void EmitWorld(WorldSnapshot& s, const world::World& world, uint32_t scriptState)
{
    EmitWorldFields(s, world, scriptState);
    EmitGrid(s, *world.m_grid);
    EmitPath(s);
    EmitObjects(s, *world.m_objectManager);
    EmitPlayer(s, *world.m_player);
    EmitFerry(s, *world.m_ferryManager);
    EmitEvents(s, *world.m_eventManager);
    EmitRepair(s, *world.m_repairManager);
    EmitFeedback(s, *world.m_feedbackManager);
    EmitMessages(s, *world.m_messageHandler);
    // MapView: the camera (section ZEngine) is presentation state, erased by Normalize, so the snapshot has no records of it.
    EmitCleaning(s, *world.m_cleaningManager);
    EmitConstruction(s, *world.m_constructionManager);
    EmitWeather(s, *world.m_weather);
}

} // namespace obl::world_snapshot
