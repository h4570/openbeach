#pragma once

// Stand-in for SDL's private SDL_internal.h, so the unit tests can include SDL's Windows scancode table
// (src/events/scancodes_windows.h) as an oracle without building SDL internals.
#include <SDL3/SDL_scancode.h>
