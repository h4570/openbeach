// Monitor query and window placement for the resolution list (src/game/ui/resolutions.cpp holds the list itself).
// Turns the window borderless when it covers the monitor, otherwise a framed window fitting the work area. The game
// window is SDL's, so it is placed through SDL: SDL's idea of its size and frame (which its WM_GETMINMAXINFO and
// WM_NCCALCSIZE enforce on Windows) always matches.

#include "platform/window_placement.h"

#include "core/config.h"
#include "game/ui/display_geometry.h"
#include "game/ui/resolutions.h"
#include "core/log.h"
#include "platform/sdl_present.h"

#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstring>

namespace obl::platform {

namespace {

// The resolution list is built at start-up, before SDL video runs (and a headless run never starts it), so the primary
// monitor comes from the system. The Linux obl-sim has no monitor: the list of a 1080p desktop.
ui::DisplaySize PrimaryMonitorSize()
{
#ifdef _WIN32
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    return {static_cast<int>(mi.rcMonitor.right - mi.rcMonitor.left), static_cast<int>(mi.rcMonitor.bottom - mi.rcMonitor.top)};
#else
    return {1920, 1080};
#endif
}

// Borderless without a sizing frame, framed with one. Positions are of the client area.
void SetSdlFrame(SDL_Window* window, bool borderless)
{
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(window);
    if (borderless) {
        SDL_SetWindowResizable(window, false);
        SDL_SetWindowBordered(window, false);
    } else {
        SDL_SetWindowBordered(window, true);
        SDL_SetWindowResizable(window, true);
    }
}

void PlaceSdlWindow(SDL_Window* window, int x, int y, int width, int height, bool borderless)
{
    SetSdlFrame(window, borderless);
    SDL_SetWindowPosition(window, x, y);
    SDL_SetWindowSize(window, width, height);
    SDL_SyncWindow(window);
    int clientW = 0, clientH = 0;
    SDL_GetWindowSizeInPixels(window, &clientW, &clientH);
    OBL_LOG("sdl window: placed %s at (%d,%d) client %dx%d (asked %dx%d)", borderless ? "borderless" : "framed", x, y, clientW, clientH,
            width, height);
}

} // namespace

void PlaceGameWindow(void* window, int width, int height)
{
    SDL_Window* sdl = SdlGameWindow(window);
    if (!sdl) return;
#ifdef __EMSCRIPTEN__
    // The canvas is the window and the page fits it into the tab (web/index.html); the "monitor" is the phone's screen, which
    // would shrink the back buffer below the game's size.
    SDL_SetWindowSize(sdl, width, height);
    return;
#endif
    SDL_DisplayID display = SDL_GetDisplayForWindow(sdl);
    if (!display) display = SDL_GetPrimaryDisplay();
    SDL_Rect monitor{}, work{};
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::strcmp(driver, "offscreen") == 0) {
        // SDL's offscreen display (hidden runs off Windows) is a fixed 1024x768; place on the desktop the resolution list
        // was built for, as a hidden Windows window does on the real monitor.
        const ui::DisplaySize desktop = platform::PrimaryMonitorSize();
        monitor = work = SDL_Rect{0, 0, desktop.cx, desktop.cy};
    } else if (!SDL_GetDisplayBounds(display, &monitor) || !SDL_GetDisplayUsableBounds(display, &work)) {
        OBL_LOG("sdl window: no display bounds (%s), window left as it is", SDL_GetError());
        return;
    }
    const int gameW = width, gameH = height;
    const ui::DisplaySize client = ui::ScaledClientSize(width, height, config::Get().scale, monitor.w, monitor.h);
    width = client.cx;
    height = client.cy;

    if (!config::Get().windowed) {
        // System fullscreen: the client always covers the monitor (Scale is ignored); the presenter and the cursor
        // mapping letterbox the game buffer inside it.
        PlaceSdlWindow(sdl, monitor.x, monitor.y, monitor.w, monitor.h, true);
        OBL_LOG("fullscreen: game buffer %dx%d, borderless client %dx%d at (%d,%d)", gameW, gameH, monitor.w, monitor.h, monitor.x,
                monitor.y);
        return;
    }
    if (ui::CoversMonitor(client, monitor.w, monitor.h)) {
        PlaceSdlWindow(sdl, monitor.x, monitor.y, width, height, true);
        OBL_LOG("resolution %dx%d: borderless on the monitor", width, height);
        return;
    }
    SetSdlFrame(sdl, false); // the frame size below is measured on the style SDL sets
    int top = 0, left = 0, bottom = 0, right = 0;
    if (!SDL_GetWindowBordersSize(sdl, &top, &left, &bottom, &right)) OBL_LOG("sdl window: no frame size (%s)", SDL_GetError());
    width = std::min(width, work.w - (left + right));
    height = std::min(height, work.h - (top + bottom));
    const int w = width + left + right, h = height + top + bottom;
    const int x = work.x + std::max(0, (work.w - w) / 2);
    const int y = work.y + std::max(0, (work.h - h) / 2);
    PlaceSdlWindow(sdl, x + left, y + top, width, height, false);
}

bool InitializeResolutions()
{
    const ui::DisplaySize monitor = platform::PrimaryMonitorSize();
    ui::BuildResolutionTable(monitor.cx, monitor.cy);
    return true;
}

} // namespace obl::platform
