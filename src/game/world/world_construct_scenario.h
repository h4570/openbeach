#pragma once

#include "game/world/world_state.h"
#include "scenario_load.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

// ConstructWorld step 2 from a loaded scenario (scenario_load.h) instead of the `.pis` file: the same texts, messages and building
// list that world_construct_pis.h reads, applied in the same order with the same Env calls, so the constructor's behavior is unchanged. The
// Env is the one of world_construct_pis.h (Duplicate, BuildingId, AddScenarioMessage).
namespace obl::world_construct_scenario {

// The optional text as the Env takes it: null when the scenario has none (Duplicate then gives the empty text the game always gave).
inline const char* Text(const std::optional<std::string>& value)
{
    return value ? value->c_str() : nullptr;
}

// The texts world_construct_pis.h::ReadTexts sets, in its order. The four objective slots are cleared first, and the count is the number
// of objectives the scenario lists (the converter allows 0..4).
template<class Env> void ApplyTexts(world::World& world, const scenario_load::Setup& s, Env& env)
{
    world.m_islandTitle = env.Duplicate(Text(s.islandTitle));
    world.m_islandArea = env.Duplicate(Text(s.islandArea));
    world.m_islandWeather = env.Duplicate(Text(s.islandWeather));
    world.m_islandTemp = env.Duplicate(Text(s.islandTemp));
    world.m_islandHazards = env.Duplicate(Text(s.islandHazards));
    world.m_scenarioDescription = env.Duplicate(s.description.c_str());
    world.m_scenarioObjectives = env.Duplicate(s.objectivesTitle.c_str());
    world.m_numObjectives = static_cast<uint32_t>(s.objectives.size());
    for (int i = 0; i < 4; ++i) {
        world.m_objectiveText[i].clear();
        world.m_objectiveStatus[i] = 0;
    }
    for (uint32_t i = 0; i < world.m_numObjectives && i < 4; ++i) world.m_objectiveText[i] = env.Duplicate(s.objectives[i].c_str());
    world.m_scenarioTitle = env.Duplicate(s.title.c_str());
}

// world_construct_pis.h::ReadMessages: each suppressed feedback type goes to the feedback manager, in order.
template<class Env> void ApplyMessages(const scenario_load::Setup& s, Env& env)
{
    for (const std::string& message : s.messages) env.AddScenarioMessage(message.c_str());
}

// Resolve scenario building keys into the cleared availability array, stopping at its 128-entry capacity.
template<class Env> void ApplyBuildings(world::World& world, const scenario_load::Setup& s, Env& env)
{
    world.m_numValidBuildings = 0;
    std::memset(world.m_validBuildingIDs, 0, sizeof(world.m_validBuildingIDs));
    for (const std::string& name : s.buildings) {
        if (world.m_numValidBuildings < 0x80) {
            world.m_validBuildingIDs[world.m_numValidBuildings] = env.BuildingId(name.c_str());
            ++world.m_numValidBuildings;
        }
    }
}

} // namespace obl::world_construct_scenario
