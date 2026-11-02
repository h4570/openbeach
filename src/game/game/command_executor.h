#pragma once

#include "game/game/command.h"

namespace obl::game {

// The outcome of one command. A rejected command changed nothing; `reason` says which check refused it (docs/architecture/commands.md).
struct CommandResult {
    bool applied = false;
    const char* reason = "";
};

// Applies one command to the live world through the game calls the GUI path uses, re-validated like that path (cash, tile flags, the
// object's type and state); ids are resolved through the object manager and an unknown id is a rejection. Needs a world.
CommandResult ApplyCommand(const command::Command& command);

} // namespace obl::game
