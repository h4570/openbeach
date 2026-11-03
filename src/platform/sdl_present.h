#pragma once

#include <cstdint>

// SDL's own name: the forward declaration must match it.
union SDL_Event; // NOLINT(readability-identifier-naming)

struct SDL_Renderer;
struct SDL_Window;
struct HWND__;

namespace obl::platform {

// The game window of the C++ WinMain, created by SDL (docs/build/settings.md): `clientWidth` x `clientHeight` with a frame,
// hidden, with the OpenBeach icon (built in). On Windows its class is "Beach Life" (SDL_RegisterApp with `instance` before
// SDL video starts; tools find the window by it) and SDL's window procedure is the only one. The game stores the returned
// window as its opaque window handle (GetGameWindow). Null on failure (logged).
SDL_Window* CreateSdlGameWindow(void* instance, int clientWidth, int clientHeight, const char* title);
// WinMain's ShowWindow(nShowCmd) through SDL (platform_main::SdlShowAction).
void ShowSdlGameWindow(int showCommand);
// WinMain teardown: the presenter released and the window destroyed.
void DestroySdlGameWindow();
// The game's opaque window handle as the SDL window it is; null for a null handle (headless) or a window SDL did not create.
SDL_Window* SdlGameWindow(void* window);
// The client size in pixels of the game window `window` (0 x 0 without one).
void GameWindowClientSize(void* window, int& width, int& height);
#ifdef _WIN32
// The Win32 handle of the game window, for the Windows-only callers (message boxes, shell links, hidden mode, test probes).
HWND__* GetWin32Window(void* window);
#endif

// The 16 bpp back buffer of the windowed renderer's Present (ui::renderer::Present): width x height pixels, top row first, `pitch` bytes per row,
// RGB555 or RGB565, letterboxed into the game window. False when SDL cannot present (logged once; the frame is dropped).
bool PresentBackBuffer(const uint16_t* bits, int pitch, int width, int height, bool rgb555);

struct SdlPresenterStats {
    bool active = false; // SDL renderer attached to the game window
    bool created = false; // SDL created the game window
    bool wrapped = false; // the game's window handle (GetGameWindow) is that SDL window
    bool icon = false; // the window shows the OpenBeach icon
    bool software = false; // the presenter fell back to SDL's software renderer
    int windowWidth = 0, windowHeight = 0; // SDL's idea of the window client size (0 while inactive)
    unsigned long long presents = 0; // frames presented through SDL
    int textureWidth = 0, textureHeight = 0;
    unsigned verifications = 0; // completed read-back checks (RequestSdlPresentVerify)
    long long lastMismatches = -1; // pixels that differ from the source buffer in the last check
    long long lastPixels = 0;
    long long lastBarPixels = 0, lastBarMismatches = -1;
};

const SdlPresenterStats& GetSdlPresenterStats();

// The presenter's SDL renderer on the game window, for output drawn before Renderer exists (intro movie). Attaches
// exactly as the first present would, so the game keeps the same SDL renderer. Null when SDL has no renderer at all.
SDL_Renderer* AttachSdlRenderer(void* window);

// Pumps SDL's event queue (which, on Windows, drains and dispatches the whole thread message queue) and takes the host's
// events: SDL_EVENT_QUIT, the game window's close request (the window is hidden rather than destroyed)
// and platform::RequestQuit (RequestSdlQuit). `quit` stays set once requested. `active`: the game window has focus (SDL focus events). Input events stay queued for the
// input adapter. `pumped` is false (queue untouched) when SDL video cannot be initialised.
struct SdlPumpResult {
    bool pumped = false;
    bool quit = false;
    int quitCode = 0;
    bool active = false;
    unsigned closeRequests = 0;
};
SdlPumpResult PumpSdlEvents();
// platform::RequestQuit: the next pump reports quit with `code`.
void RequestSdlQuit(int code);
// The close button's request without a native window message (non-Windows hosts): the next pump takes it as a close.
void PostSdlGameWindowClose();
// An event delivered like a posted window message: the next pump pushes it, so the frame that posts it does not drain it
// (a pushed SDL event would be visible to the same frame's input drain, a frame earlier than SDL's Windows backend).
void PostSdlEvent(const SDL_Event& event);
uint32_t SdlGameWindowId();

// Events that reached SDL's filter since start (input events only arrive when SDL's window gets them).
// `keyboard` counts key down/up only; device added/removed, window and focus events are in `other`.
struct SdlEventCounts {
    unsigned long long keyboard = 0, mouseMotion = 0, mouseButtons = 0, mouseWheel = 0, other = 0;
    unsigned long long mouse() const { return mouseMotion + mouseButtons + mouseWheel; }
};
SdlEventCounts GetSdlEventCounts();

// The next SDL present reads the rendered frame back and compares it with the game's buffer.
void RequestSdlPresentVerify();

} // namespace obl::platform
