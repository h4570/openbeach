#pragma once

#include "game/mods/script_runtime.h"

#include <optional>

// The world as presentation scripts read it (obl.game, docs/modding/reference/scripting.md): copied from the game's records after
// the tick, field by field, without calling into the objects, so taking it changes nothing the simulation or a test counts.
namespace obl::mods {

// The snapshot of the current world; nullopt without one.
std::optional<GameSnapshot> TakeGameSnapshot();

} // namespace obl::mods
