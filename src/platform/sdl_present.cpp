// SDL3 presentation of the windowed renderer.
//
// The renderer's Present (ui::renderer::Present) hands the 16 bpp back buffer (system memory, top row first, RGB555) to PresentBackBuffer after the
// optional fade; it is uploaded to an SDL streaming texture and presented letterboxed. Everything before it (screenshot
// capture, fade, dirty flags) is the renderer's. Without a GPU renderer SDL's software renderer presents the same way.
//
// SDL creates the game window and the game keeps the SDL_Window* as its opaque window handle. The game loop pumps SDL
// events (quit, close, focus); the input adapter consumes the input events.

#include "platform/sdl_present.h"

#include "game/game/game_accessors.h"
#include "core/config.h"
#include "core/cursor_file.h"
#include "platform/display_window.h"
#include "platform/hidden_window.h"
#include "core/log.h"
#include "core/platform_main.h"

#include <SDL3/SDL.h>
#ifdef _WIN32
#define SDL_MAIN_HANDLED // only SDL_RegisterApp: no main() of SDL's here
#include <SDL3/SDL_main.h>
#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace obl::platform {

namespace {

SdlPresenterStats g_stats;
bool g_failed;
bool g_verifyRequested;
bool g_created; // g_window is the game window SDL created; it outlives presenter failures
bool g_attached; // the presenter's renderer is set up on g_window
SDL_Window* g_window;
SDL_Renderer* g_renderer;
SDL_Texture* g_texture;
SDL_PixelFormat g_sourceFormat = SDL_PIXELFORMAT_UNKNOWN;

SdlEventCounts g_eventCounts;
bool g_quitRequested; // sticky: every loop that pumps (intro movie, then the game loop) sees it
bool g_closePosted; // PostSdlGameWindowClose: taken by the next pump like the close button
std::vector<SDL_Event> g_postedEvents; // PostSdlEvent: pushed by the next pump
int g_quitCode;
unsigned g_closeRequests; // close requests of the game window (its close button, or a posted WM_CLOSE)
bool g_active; // the game window has keyboard focus (SDL focus events)

bool SDLCALL DropEvent(void*, SDL_Event* event)
{
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: ++g_eventCounts.keyboard; return true;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (g_window && event->window.windowID == SDL_GetWindowID(g_window)) g_active = event->type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        ++g_eventCounts.other;
        return true;
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: ++g_eventCounts.other; return true; // taken by PumpSdlEvents
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: ++g_eventCounts.other; return true;
    case SDL_EVENT_MOUSE_MOTION: ++g_eventCounts.mouseMotion; return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: ++g_eventCounts.mouseButtons; return true;
    case SDL_EVENT_MOUSE_WHEEL: ++g_eventCounts.mouseWheel; return true;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_CANCELED: ++g_eventCounts.other; return true; // the touch gestures (input.cpp) read them
    default: // window, device added/removed, focus...
        ++g_eventCounts.other;
        break;
    }
    return false;
}

bool EnsureVideo()
{
    if (SDL_WasInit(SDL_INIT_VIDEO)) return true;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return false;
    SDL_SetEventFilter(&DropEvent, nullptr);
    return true;
}

void Shutdown()
{
    if (g_texture) SDL_DestroyTexture(g_texture);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    g_texture = nullptr;
    g_renderer = nullptr;
    g_attached = false;
    g_stats.active = false;
}

bool Fail(const char* what)
{
    OBL_LOG("sdl presenter: %s failed: %s - frames are dropped", what, SDL_GetError());
    Shutdown();
    g_failed = true;
    return false;
}

bool Attach()
{
    if (g_attached) return true;
    if (!g_window) {
        OBL_LOG("sdl presenter: no SDL game window");
        g_failed = true;
        return false;
    }
    // Hidden runs off Windows draw on SDL's offscreen driver, where nobody sees the output: its GL surface keeps the size the
    // window was created with, so after a resize the read-back no longer matches the window. The software renderer follows it.
    const char* driver = SDL_GetCurrentVideoDriver();
    const bool offscreen = driver && std::strcmp(driver, "offscreen") == 0;
#ifdef __EMSCRIPTEN__
    // A browser composites the canvas over the page, so a canvas with alpha would add the page's colors to the game's frame:
    // the back buffer's unused top bit reaches it as alpha 0 (EnsureTexture).
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
#endif
    g_renderer = SDL_CreateRenderer(g_window, offscreen ? SDL_SOFTWARE_RENDERER : nullptr);
    if (!g_renderer) {
        // No GPU renderer (a remote session without one, a broken driver): SDL draws the same output in software.
        OBL_LOG("sdl presenter: SDL_CreateRenderer failed: %s - trying the software renderer", SDL_GetError());
        g_renderer = SDL_CreateRenderer(g_window, SDL_SOFTWARE_RENDERER);
        if (!g_renderer) return Fail("SDL_CreateRenderer(software)");
        g_stats.software = true;
    }
    // Frame pacing belongs to the game loop and the frame limiter ([Display] MaxFps).
    SDL_SetRenderVSync(g_renderer, 0);

    g_attached = true;
    g_stats.active = true;
    OBL_LOG("sdl presenter: SDL %d.%d.%d, renderer '%s' on the game window", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION,
            SDL_GetRendererName(g_renderer));
    return true;
}

bool EnsureTexture(SDL_PixelFormat format, int width, int height)
{
    if (g_texture && format == g_sourceFormat && width == g_stats.textureWidth && height == g_stats.textureHeight) return true;
    if (g_texture) SDL_DestroyTexture(g_texture);
    // GPUs sample B5G5R5A1 but have no X1R5G5B5 format, and SDL would convert XRGB1555 on the CPU
    // every frame (~10 FPS lost at 1920x1080). The game's unused top bit is 0; with blending off the
    // alpha it becomes is ignored.
    SDL_PixelFormat textureFormat = format == SDL_PIXELFORMAT_XRGB1555 ? SDL_PIXELFORMAT_ARGB1555 : format;
    g_texture = SDL_CreateTexture(g_renderer, textureFormat, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!g_texture) return Fail("SDL_CreateTexture");
    SDL_SetTextureBlendMode(g_texture, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(g_texture, SDL_SCALEMODE_NEAREST);
    g_sourceFormat = format;
    g_stats.textureWidth = width;
    g_stats.textureHeight = height;
    OBL_LOG("sdl presenter: texture %dx%d %s from %s", width, height, SDL_GetPixelFormatName(textureFormat),
            SDL_GetPixelFormatName(format));
    return true;
}

// Largest rectangle with the source aspect ratio, centred in the output.
SDL_FRect LetterboxRect(int srcW, int srcH, int outW, int outH)
{
    screen::Rect r = ui::ImageRect(srcW, srcH, outW, outH);
    return {static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right - r.left),
            static_cast<float>(r.bottom - r.top)};
}

// Compares the rendered output inside `dst` with the source pixels (nearest sampling). GPU and SDL
// expand 5/6-bit channels slightly differently, hence the tolerance.
void Verify(const uint16_t* bits, int pitch, int width, int height, const SDL_FRect& dst)
{
    SDL_Surface* shot = SDL_RenderReadPixels(g_renderer, nullptr);
    SDL_Surface* rgb = shot ? SDL_ConvertSurface(shot, SDL_PIXELFORMAT_XRGB8888) : nullptr;
    SDL_DestroySurface(shot);
    if (!rgb) {
        OBL_LOG("sdl presenter: verify read-back failed: %s", SDL_GetError());
        return;
    }
    const SDL_PixelFormatDetails* src = SDL_GetPixelFormatDetails(g_sourceFormat);
    int x0 = static_cast<int>(dst.x), y0 = static_cast<int>(dst.y);
    int x1 = std::min(rgb->w, static_cast<int>(dst.x + dst.w)), y1 = std::min(rgb->h, static_cast<int>(dst.y + dst.h));
    long long pixels = 0, mismatches = 0;
    long long barPixels = 0, barMismatches = 0;
    for (int y = 0; y < rgb->h; ++y) {
        auto out = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(rgb->pixels) + y * rgb->pitch);
        for (int x = 0; x < rgb->w; ++x) {
            if (x >= x0 && x < x1 && y >= y0 && y < y1) continue;
            ++barPixels;
            if (out[x] & 0xffffff) ++barMismatches;
        }
    }
    // The source pixel(s) nearest to the centre of output pixel `i` of `out` pixels over `in`: at an exact tie (every third
    // pixel at 1.5x) both neighbours are nearest, and renderers differ (SDL's software scaler steps in truncated fixed point).
    struct Nearest {
        int first, last;
    };
    auto nearest = [](int i, int in, int out) {
        const long long num = (2LL * i + 1) * in, den = 2LL * out;
        const int at = static_cast<int>(std::min<long long>(in - 1, num / den));
        return Nearest{num % den == 0 && at > 0 ? at - 1 : at, at};
    };
    const int dstW = static_cast<int>(dst.w), dstH = static_cast<int>(dst.h);
    auto matches = [&](uint16_t source, uint32_t o) {
        Uint8 r, g, b;
        SDL_GetRGB(source, src, nullptr, &r, &g, &b);
        int dr = std::abs(static_cast<int>((o >> 16) & 0xff) - r);
        int dg = std::abs(static_cast<int>((o >> 8) & 0xff) - g);
        int db = std::abs(static_cast<int>(o & 0xff) - b);
        return dr <= 8 && dg <= 8 && db <= 8;
    };
    for (int y = y0; y < y1; ++y) {
        const Nearest sy = nearest(y - y0, height, dstH);
        auto out = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(rgb->pixels) + y * rgb->pitch);
        for (int x = x0; x < x1; ++x) {
            const Nearest sx = nearest(x - x0, width, dstW);
            bool match = false;
            for (int row = sy.first; row <= sy.last && !match; ++row) {
                auto line = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(bits) + row * pitch);
                for (int column = sx.first; column <= sx.last && !match; ++column) match = matches(line[column], out[x]);
            }
            ++pixels;
            if (!match) ++mismatches;
        }
    }
    SDL_DestroySurface(rgb);
    g_stats.lastPixels = pixels;
    g_stats.lastMismatches = mismatches;
    g_stats.lastBarPixels = barPixels;
    g_stats.lastBarMismatches = barMismatches;
    ++g_stats.verifications;
    OBL_LOG("sdl presenter: verify %dx%d -> rect %.0f,%.0f %.0fx%.0f pixels=%lld mismatches=%lld", width, height, dst.x, dst.y, dst.w,
            dst.h, pixels, mismatches);
}

bool Present(const uint16_t* bits, int pitch, int width, int height, SDL_PixelFormat format)
{
    if (!Attach() || !EnsureTexture(format, width, height)) return false;
    if (!SDL_UpdateTexture(g_texture, nullptr, bits, pitch)) return Fail("SDL_UpdateTexture");
    int outW = 0, outH = 0;
    SDL_GetCurrentRenderOutputSize(g_renderer, &outW, &outH);
    SDL_FRect dst = LetterboxRect(width, height, std::max(outW, 1), std::max(outH, 1));
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderTexture(g_renderer, g_texture, nullptr, &dst);
    if (g_verifyRequested) {
        g_verifyRequested = false;
        Verify(bits, pitch, width, height, dst);
    }
    if (!SDL_RenderPresent(g_renderer)) return Fail("SDL_RenderPresent");
    ++g_stats.presents;
    return true;
}

// remaster/icon.ico as the window icon; a missing or unreadable file keeps SDL's.
bool SetGameIcon(SDL_Window* window)
{
    std::ifstream file("remaster/icon.ico", std::ios::binary);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    cursor_file::Image image;
    if (data.empty() || !cursor_file::DecodeIcon(data.data(), data.size(), image)) return false;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_ARGB8888, image.argb.data(), image.width * 4);
    const bool ok = surface && SDL_SetWindowIcon(window, surface);
    SDL_DestroySurface(surface);
    return ok;
}

} // namespace

SDL_Window* CreateSdlGameWindow(void* instance, int clientWidth, int clientHeight, const char* title)
{
#ifdef _WIN32
    // The class SDL registers for its windows is the game's: tools find the window by its name.
    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        OBL_LOG("sdl window: SDL video already running, the window class keeps SDL's name");
    } else {
        if (!SDL_RegisterApp("Beach Life", CS_BYTEALIGNCLIENT | CS_OWNDC, instance))
            OBL_LOG("sdl window: SDL_RegisterApp failed: %s", SDL_GetError());
    }
#else
    (void)instance;
#endif
    if (!EnsureVideo()) {
        OBL_LOG("sdl window: SDL_InitSubSystem(VIDEO) failed: %s", SDL_GetError());
        return nullptr;
    }
    // Borderless is the game's monitor-covering mode: a plain WS_POPUP, which Windows treats as full screen (taskbar
    // below it). SDL's default borderless style keeps WS_CAPTION hidden behind its WM_NCCALCSIZE and the taskbar on top.
    SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "0");
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, clientWidth);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, clientHeight);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_Window* window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!window) {
        OBL_LOG("sdl window: SDL_CreateWindowWithProperties failed: %s", SDL_GetError());
        return nullptr;
    }
    g_window = window;
    g_created = true;
    g_stats.created = true;
    g_stats.icon = SetGameIcon(window);
    if (!g_stats.icon) OBL_LOG("sdl window: remaster/icon.ico missing or unreadable, import the game data again");
    // SDL's own window procedure stays: close, focus and input reach the game as SDL events (PumpSdlEvents, input.cpp) and the
    // local cursor is an SDL cursor (cursor.cpp).
#ifdef _WIN32
    AdoptHiddenGameWindow(GetWin32Window(window));
#endif
    OBL_LOG("sdl window: SDL created the game window (%dx%d client, class 'Beach Life')", clientWidth, clientHeight);
    return window;
}

void ShowSdlGameWindow(int showCommand)
{
    if (!g_created) return;
    const platform_main::ShowAction action = platform_main::SdlShowAction(showCommand);
    if (action != platform_main::ShowAction::Hide) SDL_ShowWindow(g_window);
    if (action == platform_main::ShowAction::ShowMinimized) SDL_MinimizeWindow(g_window);
}

void DestroySdlGameWindow()
{
    if (!g_created) return;
    g_created = false;
    g_stats.created = false;
    Shutdown();
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
    OBL_LOG("sdl window: game window destroyed");
}

SDL_Window* SdlGameWindow(void* window)
{
    return g_created && window && window == g_window ? g_window : nullptr;
}

void GameWindowClientSize(void* window, int& width, int& height)
{
    width = height = 0;
    if (SDL_Window* sdl = SdlGameWindow(window)) SDL_GetWindowSizeInPixels(sdl, &width, &height);
}

#ifdef _WIN32
HWND GetWin32Window(void* window)
{
    SDL_Window* sdl = SdlGameWindow(window);
    return sdl ? static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(sdl), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))
               : nullptr;
}
#endif

bool PresentBackBuffer(const uint16_t* bits, int pitch, int width, int height, bool rgb555)
{
    if (g_failed || !bits || width <= 0 || height <= 0) return false;
    return Present(bits, pitch, width, height, rgb555 ? SDL_PIXELFORMAT_XRGB1555 : SDL_PIXELFORMAT_RGB565);
}

SDL_Renderer* AttachSdlRenderer(void* window)
{
    if (g_failed || !SdlGameWindow(window) || !Attach()) return nullptr;
    return g_renderer;
}

const SdlPresenterStats& GetSdlPresenterStats()
{
    g_stats.windowWidth = g_stats.windowHeight = 0;
    if (g_window) SDL_GetWindowSize(g_window, &g_stats.windowWidth, &g_stats.windowHeight);
    g_stats.wrapped = g_window && game::GetGameWindow() == g_window;
    return g_stats;
}

SdlPumpResult PumpSdlEvents()
{
    SdlPumpResult result;
    if (!EnsureVideo()) return result;
    // A failed presenter only stops presenting; events still come through SDL.
    if (SdlGameWindow(game::GetGameWindow()) && !g_failed) Attach();
    SDL_PumpEvents();
    for (const SDL_Event& posted : g_postedEvents) {
        SDL_Event copy = posted;
        if (!SDL_PushEvent(&copy)) OBL_LOG("sdl window: posted event %u lost: %s", copy.type, SDL_GetError());
    }
    g_postedEvents.clear();
    // Taken here, before the input adapter drains its range (which contains the window events), so both the intro movie
    // and the game loop see them. The close button hides the window SDL owns; DestroySdlGameWindow ends it after the game.
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_QUIT, SDL_EVENT_QUIT) > 0) g_quitRequested = true;
    while (g_closePosted ||
            SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_WINDOW_CLOSE_REQUESTED, SDL_EVENT_WINDOW_CLOSE_REQUESTED) > 0) {
        if (g_closePosted) {
            g_closePosted = false;
        } else if (event.window.windowID != SdlGameWindowId()) {
            continue;
        }
        ++g_closeRequests;
        OBL_LOG("sdl window: close requested (%u), quitting", g_closeRequests);
        SDL_HideWindow(g_window);
        g_quitRequested = true;
    }
    result.pumped = true;
    result.quit = g_quitRequested;
    result.quitCode = g_quitCode;
    result.active = g_active;
    result.closeRequests = g_closeRequests;
    return result;
}

void RequestSdlQuit(int code)
{
    g_quitRequested = true;
    g_quitCode = code;
}

void PostSdlGameWindowClose()
{
    // A flag, not a pushed SDL event: the input adapter drains the window events during the frame, before the next pump.
    g_closePosted = true;
}

void PostSdlEvent(const SDL_Event& event)
{
    g_postedEvents.push_back(event);
}

uint32_t SdlGameWindowId()
{
    return g_window ? SDL_GetWindowID(g_window) : 0;
}

SdlEventCounts GetSdlEventCounts()
{
    return g_eventCounts;
}

void RequestSdlPresentVerify()
{
    g_verifyRequested = true;
}

} // namespace obl::platform
