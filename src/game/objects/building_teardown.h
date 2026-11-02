#pragma once

#include "game/objects/object_building.h"
#include "game/objects/building_thoughts.h"
#include "game/tables/world_params_tables.h"

#include <cstdint>

// Destruct releases building resources (Building's destructor runs it, before Prop's and the base object's).
// docs/architecture/objects.md. Pure logic: whatever leaves the object goes through an `Env` that the game side maps onto the game's functions
// (src/game/objects/building_destruct.cpp) and the unit test onto a recorder.
//
// Env:
// void ReleaseSprite(void* sprite) releasing the sprite through its bank
// void DeleteQueue(void* queue) queue teardown, then `delete` of the record (std::deque members)
// void DeleteEvents(BuildingEvents* events) event schedule teardown, then `delete` of the record
// void StopSound(uint32_t id) stopping the building sound
namespace obl::building_teardown {

using object::Building;

// Order: the sprite instances of the definition's ANIMSPRITEs; queue and events (destructed and deleted; the pointers stay);
// the ball sprite when `m_hasBall`; the thought list; the shortage and spanner icons (always); the building sound (and the flag cleared); the
// m_constructionSprite sprite when there is one (the pointer is cleared). The thought entries are deleted; the emptied deque goes with
// the building's members.
template<class Env> void Destruct(Building& building, Env& env)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(building.m_definition->m_NumAnimSprites); ++i)
        env.ReleaseSprite(building.m_animSprites[i]);
    if (building.m_queue) {
        env.DeleteQueue(building.m_queue);
    }
    if (building.m_events) {
        env.DeleteEvents(building.m_events);
    }
    if (building.m_hasBall) env.ReleaseSprite(building.m_ballSprite);
    thought_ops::Clear(building.m_ThoughtList);
    env.ReleaseSprite(building.m_shortageSprite);
    env.ReleaseSprite(building.m_spannerSprite);
    env.StopSound(building.m_id);
    building.m_soundPlaying = 0;
    if (building.m_constructionSprite) {
        env.ReleaseSprite(building.m_constructionSprite);
        building.m_constructionSprite = nullptr;
    }
}

} // namespace obl::building_teardown
