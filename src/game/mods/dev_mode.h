#pragma once

#include "core/char16.h"

#include <string_view>

namespace obl::mods::dev {

// Capture the startup versions before any pack loaders keep data from them.
void Start(const Text16& gameDirectory);
void Stop();

struct Changes {
    bool texts = false;
    bool gui = false;
};

// Poll the active presentation mods from the windowed frame. World files are picked up between worlds.
Changes Poll();
Changes BeforeNewGame();

bool Enabled();
std::string_view Error();

} // namespace obl::mods::dev
