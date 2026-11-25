#pragma once

// The script API of the world scripts: the built-in functions (`Function <name>;`) and the values of conditions the engine
// implements, each with the number the parser stores and the world script machine dispatches on (world_control_logic.h, world_script.cpp).
// The names are the tokens of the scripts; the parser compares them ignoring ASCII case. Messages come from
// scripts/world_vocabulary.json of the pack.

#include "game/world/world_script_parse.h"
#include "game/world/world_script_types.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace obl::world_script_parse {

struct WorldEventInfo {
    world_script::WorldEvent event;
    const char* name;
};

// Sorted by number.
const std::vector<NamedId>& EngineFunctions();
const std::vector<NamedId>& EngineValues();

// World events available to `OnEvent <Name> ... End` blocks in world scripts.
std::span<const WorldEventInfo> WorldEvents();
std::optional<world_script::WorldEvent> FindWorldEvent(std::string_view name);
const char* WorldEventName(world_script::WorldEvent event);

// Calls that a mod world script is not allowed to make (the scenario keeps the last word on winning and losing).
bool IsRefusedModFunction(int functionId);
bool IsRefusedModMenu(std::string_view dialog);

} // namespace obl::world_script_parse
