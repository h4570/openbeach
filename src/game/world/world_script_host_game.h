#pragma once

#include "game/world/world_script_host.h"
#include "game/world/world_script_types.h"
#include "game/objects/script_types.h"

// The game's script host entry points are `WorldScriptHost` members (Create, Release, Load, AddScript, Find); they are defined in
// world_script_host_game.cpp with the game's script parsers behind them. Object files call `GetWorldScriptHost()->Find` directly.
