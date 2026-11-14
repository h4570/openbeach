#pragma once

// The SDL input backend (input.cpp) behind the Input object; its test entry points are declared with the object.
#include "game/ui/input.h"

namespace obl::platform {

// The handheld build's pointer (a thumb stick, docs/build/handheld.md) in game-window client pixels; false when no stick moved it
// yet. The panel thread (frame_share.cpp) draws the cursor there.
bool HandheldCursor(int& x, int& y);

} // namespace obl::platform
