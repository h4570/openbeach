#pragma once

namespace obl::game {

using ExitHandler = int(__cdecl*)();
using RegisterExitHandler = ExitHandler(__cdecl*)(ExitHandler);

// Builds the start-up tables, the water block image and the two lookup tables. `onexit` registers the one destructor, which
// the host runs at process exit.
void RunStartup(RegisterExitHandler onexit);

// Hashes what start-up wrote, just before the host starts the game loop, for the start-up report.
void SnapshotStartupData();

void RegisterStartupReport();

} // namespace obl::game
