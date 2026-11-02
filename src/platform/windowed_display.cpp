#include "game/ui/renderer_api.h"
// Display creation; renderer construction owns the work buffers and cleanup.

#include "game/game/game_accessors.h"
#include "core/config.h"
#include "core/log.h"
#include "platform/sdl_present.h"
#include "platform/window_placement.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstring>

namespace obl::ui {
namespace {

// The client rectangle in screen coordinates; headless (no window): a window at the origin.
screen::Rect WindowScreenRect(void* window, int width, int height)
{
#ifdef _WIN32 // the Linux obl-sim has no presenter module
    if (SDL_Window* sdl = platform::SdlGameWindow(window)) {
        int x = 0, y = 0, w = 0, h = 0;
        SDL_GetWindowPosition(sdl, &x, &y);
        SDL_GetWindowSizeInPixels(sdl, &w, &h);
        return {x, y, x + w, y + h};
    }
#else
    (void)window;
#endif
    return {0, 0, width, height};
}

bool CreateDisplayImpl(ui::Renderer* display, void* window, bool windowed, int width, int height, int bpp)
{
    if (!windowed) return false; // The renderer always requests the windowed display path.

    if (!display->m_windowed || bpp != 16 || width <= 0 || height <= 0) {
        OBL_LOG("system display: invalid constructor state");
        return false;
    }
    for (uint8_t& supported : display->m_modeSupported) supported = 1;
    if (window) platform::PlaceGameWindow(window, width, height);
    display->m_screenRect = WindowScreenRect(window, width, height);

    int surface = obl::ui::renderer::CreateSurface(display, width, height, 0);
    if (surface != 0) {
        OBL_LOG("system display: first surface was %d instead of 0", surface);
        return false;
    }
    ui::RendererSurface* back = display->m_surfaces[0];
    back->m_originX = 0;
    back->m_originY = 0;

    display->m_rgb555 = 1;
    display->m_greenMask = 0x3e0;
    OBL_LOG("system display: %dx%d RGB555, surface 0", width, height);
    return true;
}

} // namespace

bool ui::renderer::CreateDisplay(void* display, void* window, bool windowed, int width, int height, int bpp)
{
    return CreateDisplayImpl(static_cast<ui::Renderer*>(display), window, windowed, width, height, bpp);
}

} // namespace obl::ui
