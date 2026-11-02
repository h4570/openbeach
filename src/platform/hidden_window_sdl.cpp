// Hidden mode on the windowed hosts without Win32 (docs/architecture/portable-host.md): the game runs on SDL's offscreen
// video driver, so no window ever appears or takes focus. Windows keeps its own window tricks (hidden_window.cpp).

#include "platform/hidden_window.h"

#include "core/log.h"

#include <SDL3/SDL_hints.h>

#include <cstdlib>

namespace obl::platform {

void AdoptHiddenGameWindow(HWND__*) {}

bool StartHiddenMode()
{
    if (!std::getenv("OBL_HIDDEN")) return true;
    // Before SDL video starts (the game window is created by WinMain, after the installers).
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    OBL_LOG("hidden mode: on (OBL_HIDDEN), SDL offscreen video driver");
    return true;
}

} // namespace obl::platform
