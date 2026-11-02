#pragma once

// Standalone host (docs/architecture/portable-host.md): a copy of OpenBeach.exe started with `--standalone-host`, which
// starts the game in its own process (OpenGame) and runs it (game::RunGame). Test tools find a game directory's host by
// that flag (tools/common.ps1).

#include <string>

namespace obl::standalone {

// Exit codes of OpenBeach.exe besides the game's own.
constexpr int kExitExeMissing = 77; // ctest SKIP: run/ is not prepared
constexpr int kExitInitFailed = 4; // the start-up failed or its log shows a failure

// Launcher side: creates the host process and waits for its exit code when `wait` is set.
// `gameArgs`: the game's own arguments (after `--`), each with its leading space. `runEntry` false (`--no-entry`, ctest
// standalone_host): the host ends after the start-up, without a game window.
int Launch(const std::string& gameDir, const std::string& gameArgs, bool wait, bool runEntry);

// Host side (`--standalone-host`): runs in the process Launch created, starts the game and returns its exit code.
int HostMain(const std::string& gameDir, const std::string& gameArgs, bool runEntry);

} // namespace obl::standalone
