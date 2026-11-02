#pragma once

struct HWND__; // HWND without <windows.h>: its `far`/`near` macros break headers included after this one

namespace obl::platform {

// Test-only: keeps the game window off-screen and unfocused when OBL_HIDDEN is set
// (launcher --hidden).
bool StartHiddenMode();

// The game window SDL created (still hidden): gets the hidden-mode styles and z-order the CreateWindowExA hook gives the
// game's own window. Does nothing outside hidden mode.
void AdoptHiddenGameWindow(HWND__* hwnd);

} // namespace obl::platform
