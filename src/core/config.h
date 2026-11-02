#pragma once

#include "core/char16.h"
#include <string>

namespace obl::config {

struct Settings {
    bool windowed = true; // [Display] Windowed
    bool playIntroMovie = true; // [Intro] PlayMovie
    int maxFps = 60; // [Display] MaxFps (0 = unlimited)
    double scale = 1.0; // [Display] Scale (1..4, window size only)
    // OBL_HEADLESS (launcher --headless <replay|script>): no window, renderer output, audio or input devices
    // (docs/architecture/headless/index.md). Not an ini key. With OBL_SCRIPT as well the headless loop runs that test script
    // (headlessScript) instead of a replay.
    bool headless = false;
    bool headlessScript = false;
    Text16 headlessReplay;
};

// Loads openbeach.ini from the game directory; writes a default file if it does not exist.
void Load(const Text16& gameDirectory);

const Settings& Get();

} // namespace obl::config
