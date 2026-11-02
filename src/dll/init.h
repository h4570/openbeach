#pragma once

#include "core/char16.h"
#include "platform/windows_only.h"

#include <filesystem>
#include <string>

namespace obl {

// Start-up after the log is open: config, packs and every module's init and registration. 0 on success, otherwise the start-up's failure
// code (4 a module init, 8 the pack). Called by the Windows
// OpenGame (dllmain.cpp) and the Linux obl-sim (src/host_linux/main.cpp); `gameDir` ends with a separator.
int InitGame(const Text16& gameDir);

// The game executable's start-up (src/launcher): opens the game directory's log, then InitGame. `gameDir` is absolute with a
// trailing separator.
int OpenGame(const std::filesystem::path& gameDir);

} // namespace obl
