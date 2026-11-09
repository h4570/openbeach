#include "settings_window.h"

#include "core/cursor_file.h"
#include "core/pack_path.h"
#include "game/game/start_settings.h"
#include "openbeach_icon.gen.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace obl::launcher {

namespace {

namespace fs = std::filesystem;
namespace settings = game::start_settings;

// The layout in units of the built-in 8x8 font; the window shows it at a whole multiple, so the font stays crisp.
constexpr int kWidth = 432;
constexpr int kHeight = 316;
constexpr float kChar = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
constexpr float kHeaderHeight = 48;
constexpr float kRowTop = 60;
constexpr float kRowHeight = 28;
constexpr float kValueLeft = 236;
constexpr float kValueRight = 412;
constexpr float kButtonTop = 270;
constexpr float kButtonHeight = 28;
constexpr SDL_FRect kQuitButton{212, kButtonTop, 90, kButtonHeight};
constexpr SDL_FRect kPlayButton{316, kButtonTop, 96, kButtonHeight};

struct Color {
    Uint8 r, g, b;
};
constexpr Color kSand{245, 230, 196};
constexpr Color kSea{32, 110, 170};
constexpr Color kSeaLight{70, 150, 210};
constexpr Color kFoam{220, 236, 248};
constexpr Color kInk{40, 40, 40};
constexpr Color kHint{125, 112, 88};
constexpr Color kDisabled{170, 160, 140};
constexpr Color kFocus{252, 243, 222};
constexpr Color kBox{255, 252, 242};
constexpr Color kBorder{200, 180, 140};
constexpr Color kWhite{255, 255, 255};

fs::path Utf8Path(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

fs::path PackDirectory(const fs::path& gameDir)
{
    const fs::path selected = pack_path::Selected();
    return selected.empty() ? fs::path() : gameDir / selected;
}

enum Row { Language, Display, Size, Mouse, FrameCap, Movie, ShowAtStart, kRowCount };
// Keyboard focus: a row, then the two buttons.
constexpr int kQuitFocus = kRowCount;
constexpr int kPlayFocus = kRowCount + 1;

struct RowText {
    const char* label;
    const char* hint;
    std::string value;
    bool enabled;
};

#ifdef __APPLE__
constexpr const char* kSwitchHint = "Option+Return in game too";
#else
constexpr const char* kSwitchHint = "Alt+Enter switches in game";
#endif

const char* OnOff(bool value)
{
    return value ? "On" : "Off";
}

std::string ScaleName(double scale)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%gx", scale);
    return text;
}

std::string FrameCapName(int fps)
{
    return fps > 0 ? std::to_string(fps) + " fps" : std::string("Unlimited");
}

// `list` with `value` in it: a value from the file that the window does not offer stays selectable.
template<class T> std::vector<T> WithValue(std::vector<T> list, T value)
{
    if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
    return list;
}

template<class T> T Step(const std::vector<T>& list, T value, int delta)
{
    const auto at = static_cast<int>(std::find(list.begin(), list.end(), value) - list.begin());
    const int count = static_cast<int>(list.size());
    return list[static_cast<size_t>(((at + delta) % count + count) % count)];
}

class Window {
public:
    explicit Window(settings::Values values) : m_values(std::move(values))
    {
        m_scales = WithValue(std::vector<double>(std::begin(settings::kScales), std::end(settings::kScales)), m_values.scale);
        std::sort(m_scales.begin(), m_scales.end());
        m_frameCaps = WithValue(std::vector<int>(std::begin(settings::kFrameCaps), std::end(settings::kFrameCaps)), m_values.maxFps);
        // Unlimited (0) stays last.
        std::stable_partition(m_frameCaps.begin(), m_frameCaps.end(), [](int fps) { return fps > 0; });
        std::sort(m_frameCaps.begin(), m_frameCaps.end() - (m_frameCaps.back() == 0 ? 1 : 0));
    }

    const settings::Values& Values() const { return m_values; }

    RowText Text(int row) const
    {
        const settings::Values& v = m_values;
        switch (row) {
        case Language: {
            const bool choice = v.languages.size() > 1;
            return {"Language", choice ? "imported from your copy" : "the only one in this copy",
                    v.languages.empty() ? std::string("-") : std::string(settings::LanguageName(v.language)), choice};
        }
        case Display: return {"Display", kSwitchHint, v.windowed ? "Window" : "Fullscreen", true};
        case Size: return {"Window size", "capped at the screen size", ScaleName(v.scale), v.windowed};
        case Mouse: return {"Keep mouse in window", "the edge scrolls the map", OnOff(v.confineMouse), true};
        case FrameCap: return {"Frame rate cap", "frames per second", FrameCapName(v.maxFps), true};
        case Movie: return {"Intro movie", "the movie before the menu", OnOff(v.playMovie), true};
        default: return {"Show this window", "off: start with --settings", OnOff(v.showAtStart), true};
        }
    }

    void Change(int row, int delta)
    {
        if (row < 0 || row >= kRowCount || !Text(row).enabled) return;
        settings::Values& v = m_values;
        switch (row) {
        case Language: v.language = Step(v.languages, v.language, delta); break;
        case Display: v.windowed = !v.windowed; break;
        case Size: v.scale = Step(m_scales, v.scale, delta); break;
        case Mouse: v.confineMouse = !v.confineMouse; break;
        case FrameCap: v.maxFps = Step(m_frameCaps, v.maxFps, delta); break;
        case Movie: v.playMovie = !v.playMovie; break;
        default: v.showAtStart = !v.showAtStart; break;
        }
    }

private:
    settings::Values m_values;
    std::vector<double> m_scales;
    std::vector<int> m_frameCaps;
};

void SetColor(SDL_Renderer* renderer, Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
}

void Fill(SDL_Renderer* renderer, const SDL_FRect& rect, Color color)
{
    SetColor(renderer, color);
    SDL_RenderFillRect(renderer, &rect);
}

void Frame(SDL_Renderer* renderer, const SDL_FRect& rect, Color color)
{
    SetColor(renderer, color);
    SDL_RenderRect(renderer, &rect);
}

void Print(SDL_Renderer* renderer, float x, float y, const std::string& text, Color color, float size = 1)
{
    SetColor(renderer, color);
    SDL_SetRenderScale(renderer, size, size);
    SDL_RenderDebugText(renderer, x / size, y / size, text.c_str());
    SDL_SetRenderScale(renderer, 1, 1);
}

float TextWidth(const std::string& text)
{
    return kChar * static_cast<float>(text.size());
}

void Button(SDL_Renderer* renderer, const SDL_FRect& rect, const std::string& label, bool primary, bool focused)
{
    if (primary) {
        Fill(renderer, rect, focused ? kSeaLight : kSea);
    } else {
        Fill(renderer, rect, focused ? kFocus : kBox);
        Frame(renderer, rect, kBorder);
    }
    if (focused && !primary) Frame(renderer, SDL_FRect{rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4}, kSea);
    if (focused && primary) Frame(renderer, SDL_FRect{rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4}, kWhite);
    Print(renderer, rect.x + (rect.w - TextWidth(label)) / 2, rect.y + (rect.h - kChar) / 2, label, primary ? kWhite : kInk);
}

SDL_FRect RowRect(int row)
{
    return {10, kRowTop + row * kRowHeight, static_cast<float>(kWidth) - 20, kRowHeight};
}

SDL_FRect ValueBox(int row)
{
    return {kValueLeft, kRowTop + row * kRowHeight + 4, kValueRight - kValueLeft, kRowHeight - 8};
}

void Draw(SDL_Renderer* renderer, const Window& window, int focus)
{
    SetColor(renderer, kSand);
    SDL_RenderClear(renderer);
    Fill(renderer, SDL_FRect{0, 0, static_cast<float>(kWidth), kHeaderHeight}, kSea);
    Print(renderer, 20, 9, "OpenBeach", kWhite, 2);
    Print(renderer, 20, 31, "Settings before you play", kFoam);

    for (int row = 0; row < kRowCount; ++row) {
        const RowText text = window.Text(row);
        const SDL_FRect rect = RowRect(row);
        if (row == focus) Fill(renderer, rect, kFocus);
        Print(renderer, 20, rect.y + 5, text.label, kInk);
        Print(renderer, 20, rect.y + 16, text.hint, kHint);
        const SDL_FRect box = ValueBox(row);
        Fill(renderer, box, kBox);
        Frame(renderer, box, row == focus ? kSea : kBorder);
        const float textY = box.y + (box.h - kChar) / 2;
        Print(renderer, box.x + (box.w - TextWidth(text.value)) / 2, textY, text.value, text.enabled ? kInk : kDisabled);
        if (text.enabled) {
            Print(renderer, box.x + 6, textY, "<", kSea);
            Print(renderer, box.x + box.w - 6 - kChar, textY, ">", kSea);
        }
    }

    Print(renderer, 20, kButtonTop + (kButtonHeight - kChar) / 2, "Enter: play  Esc: quit", kHint);
    Button(renderer, kQuitButton, "Quit", false, focus == kQuitFocus);
    Button(renderer, kPlayButton, "Play", true, focus == kPlayFocus);
    SDL_RenderPresent(renderer);
}

bool Inside(const SDL_FRect& rect, float x, float y)
{
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

// The row or button under the pointer, -1 for none.
int HitTest(float x, float y)
{
    if (Inside(kQuitButton, x, y)) return kQuitFocus;
    if (Inside(kPlayButton, x, y)) return kPlayFocus;
    for (int row = 0; row < kRowCount; ++row)
        if (Inside(RowRect(row), x, y)) return row;
    return -1;
}

// A whole multiple of the layout that keeps its 8-unit font readable on the display (twice the desktop scale) and fits on it.
int WindowMultiple()
{
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    float contentScale = display ? SDL_GetDisplayContentScale(display) : 1.0f;
    if (contentScale <= 0) contentScale = 1.0f;
    int multiple = std::max(1, static_cast<int>(std::lround(2.0f * contentScale)));
    SDL_Rect usable{};
    if (display && SDL_GetDisplayUsableBounds(display, &usable)) {
        while (multiple > 1 && (kWidth * multiple > usable.w * 9 / 10 || kHeight * multiple > usable.h * 9 / 10)) --multiple;
    }
    return multiple;
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
bool RunLoop(SDL_Renderer* renderer, Window& window)
{
    int focus = kPlayFocus;
    Draw(renderer, window, focus);
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
            if (const int hit = HitTest(event.motion.x, event.motion.y); hit >= 0) focus = hit;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event.button.button != SDL_BUTTON_LEFT) break;
            const int hit = HitTest(event.button.x, event.button.y);
            if (hit == kQuitFocus) return false;
            if (hit == kPlayFocus) return true;
            if (hit < 0) break;
            focus = hit;
            // The left half of the value box goes back, anything else forward.
            const SDL_FRect box = ValueBox(hit);
            window.Change(hit, Inside(box, event.button.x, event.button.y) && event.button.x < box.x + box.w / 2 ? -1 : 1);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            if (focus < kRowCount && event.wheel.y != 0) window.Change(focus, event.wheel.y > 0 ? -1 : 1);
            break;
        default: break;
        }
        Draw(renderer, window, focus);
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
    const int multiple = WindowMultiple();
    SDL_Window* sdlWindow = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool play = true;
    if (SDL_CreateWindowAndRenderer("OpenBeach", kWidth * multiple, kHeight * multiple, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN,
                &sdlWindow, &renderer)) {
        SetIcon(sdlWindow);
        SDL_SetRenderVSync(renderer, 1);
        SDL_SetRenderLogicalPresentation(renderer, kWidth, kHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_ShowWindow(sdlWindow);
        SDL_RaiseWindow(sdlWindow);
        Window window(before);
        play = RunLoop(renderer, window);
        if (play && !settings::Save(dir, before, window.Values()))
            std::fprintf(stderr, "settings window: could not store every setting in %s\n", gameDir.c_str());
    } else {
        std::fprintf(stderr, "settings window: cannot open (%s), starting the game\n", SDL_GetError());
    }
    if (renderer) SDL_DestroyRenderer(renderer);
    if (sdlWindow) SDL_DestroyWindow(sdlWindow);
    // The game initializes video again with its own hints (the window class name on Windows, Linux's app id).
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return play ? SettingsChoice::Play : SettingsChoice::Quit;
}

} // namespace obl::launcher
