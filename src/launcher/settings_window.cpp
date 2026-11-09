#include "settings_window.h"

#include "core/cursor_file.h"
#include "core/pack_path.h"
#include "game/game/start_settings.h"
#include "openbeach_icon.gen.h"
#include "settings_view.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace obl::launcher {

namespace {

namespace fs = std::filesystem;
namespace settings = game::start_settings;
namespace view = settings_view;
using view::kPlayFocus;
using view::kQuitFocus;
using view::kRowCount;

fs::path Utf8Path(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

fs::path PackDirectory(const fs::path& gameDir)
{
    const fs::path selected = pack_path::Selected();
    return selected.empty() ? fs::path() : gameDir / selected;
}

// The display's scale and usable area decide the window's scale of the layout.
float WindowScale()
{
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const float contentScale = display ? SDL_GetDisplayContentScale(display) : 1.0f;
    SDL_Rect usable{};
    if (!display || !SDL_GetDisplayUsableBounds(display, &usable)) usable = SDL_Rect{};
    return view::WindowScale(contentScale, usable.w, usable.h);
}

void SetIcon(SDL_Window* window)
{
    cursor_file::Image image;
    if (!cursor_file::DecodeIcon(kOpenBeachIcon, sizeof(kOpenBeachIcon), image)) return;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_ARGB8888, image.argb.data(), image.width * 4);
    if (surface) SDL_SetWindowIcon(window, surface);
    SDL_DestroySurface(surface);
}

// The event loop; true = Play.
bool RunLoop(SDL_Renderer* renderer, SDL_Texture* layout, view::Model& window)
{
    int focus = kPlayFocus;
    const auto show = [&] {
        view::Present(renderer, layout, window, focus);
        SDL_RenderPresent(renderer);
    };
    show();
    for (;;) {
        SDL_Event event;
        if (!SDL_WaitEvent(&event)) return true;
        SDL_ConvertEventToRenderCoordinates(renderer, &event);
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return false;
        case SDL_EVENT_KEY_DOWN:
            switch (event.key.key) {
            case SDLK_ESCAPE: return false;
            case SDLK_RETURN:
            // Always Play: the pointer may rest on Quit and give it the focus without the player noticing.
            case SDLK_KP_ENTER: return true;
            case SDLK_UP: focus = (focus + kPlayFocus) % (kPlayFocus + 1); break;
            case SDLK_DOWN:
            case SDLK_TAB: focus = (focus + 1) % (kPlayFocus + 1); break;
            case SDLK_LEFT:
            case SDLK_RIGHT: {
                const int delta = event.key.key == SDLK_LEFT ? -1 : 1;
                if (focus < kRowCount)
                    window.Change(focus, delta);
                else
                    focus = focus == kQuitFocus ? kPlayFocus : kQuitFocus;
                break;
            }
            case SDLK_SPACE:
                if (focus == kQuitFocus) return false;
                if (focus == kPlayFocus) return true;
                window.Change(focus, 1);
                break;
            default: break;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (const int hit = view::HitTest(event.motion.x, event.motion.y); hit >= 0) focus = hit;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event.button.button != SDL_BUTTON_LEFT) break;
            const int hit = view::HitTest(event.button.x, event.button.y);
            if (hit == kQuitFocus) return false;
            if (hit == kPlayFocus) return true;
            if (hit < 0) break;
            focus = hit;
            // The left half of the value box goes back, anything else forward.
            const SDL_FRect box = view::ValueBox(hit);
            window.Change(hit, view::Inside(box, event.button.x, event.button.y) && event.button.x < box.x + box.w / 2 ? -1 : 1);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            if (focus < kRowCount && event.wheel.y != 0) window.Change(focus, event.wheel.y > 0 ? -1 : 1);
            break;
        default: break;
        }
        show();
    }
}

} // namespace

bool ShouldShowSettings(const std::string& gameDir, bool force)
{
    const fs::path dir = Utf8Path(gameDir);
    const fs::path pack = PackDirectory(dir);
    std::error_code error;
    // Without a pack the game cannot start, and its own message says what to import.
    if (pack.empty() || !fs::is_regular_file(pack / "pack.json", error)) return false;
    return force || settings::Load(dir, pack).showAtStart;
}

SettingsChoice RunSettingsWindow(const std::string& gameDir)
{
    const fs::path dir = Utf8Path(gameDir);
    const settings::Values before = settings::Load(dir, PackDirectory(dir));
#ifndef _WIN32
    SDL_SetHint(SDL_HINT_APP_ID, "openbeach");
#endif
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "settings window: no video (%s), starting the game\n", SDL_GetError());
        return SettingsChoice::Play;
    }
    const float scale = WindowScale();
    SDL_Window* sdlWindow = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* layout = nullptr;
    bool play = true;
    if (SDL_CreateWindowAndRenderer("OpenBeach", static_cast<int>(std::lround(view::kWidth * scale)),
                static_cast<int>(std::lround(view::kHeight * scale)), SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN, &sdlWindow,
                &renderer) &&
            (layout = view::LayoutTexture(renderer, scale)) != nullptr) {
        SetIcon(sdlWindow);
        SDL_SetRenderVSync(renderer, 1);
        SDL_SetRenderLogicalPresentation(renderer, view::kWidth, view::kHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_ShowWindow(sdlWindow);
        // A shortcut that starts the program minimized (the installer's does, to keep the console out of the way) hands
        // that state to the process's first window, which is this one: the player would only see a taskbar button.
        if (SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(sdlWindow);
        SDL_RaiseWindow(sdlWindow);
        view::Model window(before);
        play = RunLoop(renderer, layout, window);
        if (play && !settings::Save(dir, before, window.Values()))
            std::fprintf(stderr, "settings window: could not store every setting in %s\n", gameDir.c_str());
    } else {
        std::fprintf(stderr, "settings window: cannot open (%s), starting the game\n", SDL_GetError());
    }
    if (layout) SDL_DestroyTexture(layout);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (sdlWindow) SDL_DestroyWindow(sdlWindow);
    // The game initializes video again with its own hints (the window class name on Windows, Linux's app id).
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return play ? SettingsChoice::Play : SettingsChoice::Quit;
}

} // namespace obl::launcher
