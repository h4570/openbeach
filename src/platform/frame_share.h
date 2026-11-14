#pragma once

// The handheld build's panel (docs/build/handheld.md): the game hands its frames to a thread that shows them through the firmware's
// own SDL2 and handles the console's volume keys (frame_share.cpp).

#include <cstdint>

namespace obl::platform {

struct SharedCursor {
    bool visible = false;
    int x = 0, y = 0; // game pixels
};

// Copies the 16 bpp back buffer for the panel thread, which the first call starts. False when the panel cannot show it (logged).
bool SharedFramePresent(const uint16_t* bits, int pitch, int width, int height, bool rgb555, const SharedCursor& cursor);
// Ends the panel thread: the console's volume is put back and the window closed.
void SharedFrameClose();

} // namespace obl::platform
