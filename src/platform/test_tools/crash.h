#pragma once

#include "core/watchdog.h"

#include <string>
#include <cstdint>

namespace obl::test {

// Script mode only: logs an unhandled exception (code, address, module, registers, return-address candidates) as
// `autotest: CRASH ...` lines and ends the process. tools/autotest.ps1 prints module offsets and available PDB names.
// Installs a redirect that prevents the game/CRT from replacing the filter; emergency logging bypasses the log mutex.
bool InitCrashDiagnostics();

// Called once per script frame (including every `step` frame): the hang watchdog samples this thread and ends the process when
// 20 s pass without a beat once the script has started.
void WatchdogBeat();

// Scope for work that legitimately runs long inside one frame (`call-drive`, the world load): the hang limit becomes 120 s for its
// duration. A real hang inside it is still reported by the watchdog (with the sampled stacks), only later.
using WatchdogLongCommand = obl::WatchdogLongCommand;

// crash-test: calls Building::OnExitState with a null `this` - a deliberate crash inside game code, to check the report
// -hang blocks the game thread (hang watchdog report), -log-lock crashes under the log mutex, -filter tries removing the filter first. crash-handled checks nonfatal SEH.
// tools/test_crash_diagnostics.ps1 verifies the expected fatal reports; they are outside tests/manifest.txt.
bool ExecuteCrashCommand(const std::string& name);

} // namespace obl::test
