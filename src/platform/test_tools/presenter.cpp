#include "presenter.h"

#include "platform/sdl_present.h"
#include "platform/audio_output.h"
#include "platform/display_window.h"
#include "platform/input.h"
#include "platform/cursor.h"
#include "game/game/game_accessors.h"
#include "game/ui/renderer_types.h"
#include "game/ui/input_types.h"
#include "game/ui/map_view_access.h"
#include "platform/window_placement.h"
#include "core/config.h"
#include "core/log.h"

#include <SDL3/SDL.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace obl::test {

namespace {

// The game window as the platform names it: the HWND under SDL on Windows, the SDL window elsewhere. The native messages
// (posted keys and mouse, window sizing) go through Win32 on Windows and are pushed as SDL events elsewhere.
#ifdef _WIN32
using TestWindowHandle = HWND;
TestWindowHandle GetTestWindowHandle(void* window)
{
    return platform::GetWin32Window(window);
}
#else
using TestWindowHandle = SDL_Window*;
TestWindowHandle GetTestWindowHandle(void* window)
{
    return window ? SDL_GetWindowFromID(platform::SdlGameWindowId()) : nullptr;
}

// The virtual-key codes the scripts post, as SDL scancodes.
SDL_Scancode ScancodeOfVirtualKey(int vk)
{
    if (vk >= 'A' && vk <= 'Z') return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (vk - 'A'));
    if (vk >= '1' && vk <= '9') return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (vk - '1'));
    switch (vk) {
    case '0': return SDL_SCANCODE_0;
    case 0x08: return SDL_SCANCODE_BACKSPACE;
    case 0x09: return SDL_SCANCODE_TAB;
    case 0x0d: return SDL_SCANCODE_RETURN;
    case 0x1b: return SDL_SCANCODE_ESCAPE;
    case 0x20: return SDL_SCANCODE_SPACE;
    case 0x25: return SDL_SCANCODE_LEFT;
    case 0x26: return SDL_SCANCODE_UP;
    case 0x27: return SDL_SCANCODE_RIGHT;
    case 0x28: return SDL_SCANCODE_DOWN;
    case 0xbb: return SDL_SCANCODE_EQUALS;
    case 0xbd: return SDL_SCANCODE_MINUS;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

void PostMouseMotion(float x, float y, bool left)
{
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = platform::SdlGameWindowId();
    e.motion.x = x;
    e.motion.y = y;
    e.motion.state = left ? SDL_BUTTON_LMASK : 0;
    platform::PostSdlEvent(e);
}
#endif

// A focus change as SDL reports it for the game window (input.cpp's event watch acts on it at once). SDL's Windows backend
// makes these events only for the foreground window, so a hidden run's tests push them.
bool PushSdlFocus(bool gain)
{
    SDL_Event e{};
    e.type = gain ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    e.window.windowID = platform::SdlGameWindowId();
    return SDL_PushEvent(&e);
}

// The ui::Input fields the probes read through the object's opaque handle (a byte pointer).
using InputInt = int ui::Input::*;
constexpr InputInt kLeftButton = &ui::Input::m_leftButton;
constexpr InputInt kRightButton = &ui::Input::m_rightButton;
constexpr InputInt kDeltaX = &ui::Input::m_deltaX;
constexpr InputInt kDeltaY = &ui::Input::m_deltaY;
constexpr InputInt kWheel = &ui::Input::m_wheel;
constexpr InputInt kMouseX = &ui::Input::m_x;
constexpr InputInt kMouseY = &ui::Input::m_y;
constexpr InputInt kRecordCount = &ui::Input::m_recordCount;

ui::Input& InputOf(uint8_t* handle)
{
    return *reinterpret_cast<ui::Input*>(handle);
}

// The key slot `code` (input::KeyCode) of an Input object.
uint8_t KeyState(uint8_t* handle, int code)
{
    return InputOf(handle).m_keys[code];
}

// Key slots the probes name (physical-key codes of the Input object).
constexpr int kKeyA = 0x1e;
constexpr int kKeyZ = 0x2c;
constexpr int kKeyLeftCtrl = 0x1d;
constexpr int kKeyRightShift = 0x36;
constexpr int kKeyRightAlt = 0xb8;

void CheckInput()
{
    auto* active = game::GetInput();
    ui::Input local;
    memset(&local, 0xa5, sizeof(local));
    auto* result = ui::InputConstruct(local, 800, 600, true);
    bool ok = result == &local && game::GetInput() == &local && local.m_width == 800 && local.m_height == 600 && local.m_windowed &&
            !local.m_directInput && !local.m_mouseDevice && !local.m_keyboardDevice;
    for (uint8_t state : local.m_keys) ok &= state == 0;
    ui::InputSetScreenSize(local, 1280, 960);
    ok &= local.m_width == 1280 && local.m_height == 960;
    for (int code = 0; code < 256; ++code) {
        for (uint8_t state : std::initializer_list<uint8_t>{0, 1, 2, 3}) {
            local.m_keys[code] = state;
            ok &= ui::InputKeyHeld(local, code) == (state == 2);
            ok &= ui::InputKeyPressed(local, code) == (state == 1);
        }
    }
    bool init = ui::InputInit(local, active->m_module, active->m_window);
    ok &= init && local.m_window == active->m_window && local.m_module == active->m_module && local.m_mouseDevice;
    ok &= !local.m_directInput && !local.m_keyboardDevice;
    ui::InputDestruct(local);
    ok &= !game::GetInput() && !local.m_directInput && !local.m_keyboardDevice && !local.m_mouseDevice;
    game::SetInput(active);
    OBL_LOG("autotest: %s input-object-contract lifecycle/size/all-key-states", ok ? "PASS" : "FAILED");
}

void CheckSdlKeyboard(TestWindowHandle hwnd)
{
    if (!hwnd || !platform::SdlGameWindowId()) {
        OBL_LOG("autotest: FAILED keyboard-contract needs the SDL keyboard and a wrapped window");
        return;
    }
    auto update = +[](uint8_t* input, void*) { return ui::InputUpdateKeyboard(*reinterpret_cast<ui::Input*>(input)); };
    auto updateAll = +[](uint8_t* input, void*) { return ui::InputUpdate(*reinterpret_cast<ui::Input*>(input)); };
    auto held = +[](uint8_t* input, void*, int code) { return ui::InputKeyHeld(*reinterpret_cast<ui::Input*>(input), code); };
    auto pressed = +[](uint8_t* input, void*, int code) { return ui::InputKeyPressed(*reinterpret_cast<ui::Input*>(input), code); };
    auto input = reinterpret_cast<uint8_t*>(game::GetInput());
    uint8_t scratch[sizeof(ui::Input)]{};
    auto reset = [&] {
        PushSdlFocus(false);
        PushSdlFocus(true);
        update(scratch, nullptr);
    };
    auto key = [&](SDL_Scancode scan, bool down, bool repeat = false, uint32_t window = 0) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = window ? window : platform::SdlGameWindowId();
        e.key.scancode = scan;
        e.key.down = down;
        e.key.repeat = repeat;
        return SDL_PushEvent(&e);
    };
    reset();
    bool mapping = true, states = true;
    // Expected indices are the stable physical-key contract, including distinct extended/numpad keys.
    const std::pair<SDL_Scancode, int> cases[] = {{SDL_SCANCODE_A, 0x1e}, {SDL_SCANCODE_Z, 0x2c}, {SDL_SCANCODE_1, 0x02},
            {SDL_SCANCODE_0, 0x0b}, {SDL_SCANCODE_RETURN, 0x1c}, {SDL_SCANCODE_KP_ENTER, 0x9c}, {SDL_SCANCODE_KP_8, 0x48},
            {SDL_SCANCODE_UP, 0xc8}, {SDL_SCANCODE_DOWN, 0xd0}, {SDL_SCANCODE_LEFT, 0xcb}, {SDL_SCANCODE_RIGHT, 0xcd},
            {SDL_SCANCODE_LSHIFT, 0x2a}, {SDL_SCANCODE_RSHIFT, 0x36}, {SDL_SCANCODE_LCTRL, 0x1d}, {SDL_SCANCODE_RCTRL, 0x9d},
            {SDL_SCANCODE_LALT, 0x38}, {SDL_SCANCODE_RALT, 0xb8}, {SDL_SCANCODE_F1, 0x3b}, {SDL_SCANCODE_F12, 0x58},
            {SDL_SCANCODE_HOME, 0xc7}, {SDL_SCANCODE_DELETE, 0xd3}, {SDL_SCANCODE_NONUSBACKSLASH, 0x56}, {SDL_SCANCODE_PAUSE, 0xc5}};
    for (const auto& [scan, code] : cases) {
        states &= key(scan, true) && update(scratch, nullptr);
        mapping &= KeyState(scratch, code) == 1 && pressed(scratch, nullptr, code) && !held(scratch, nullptr, code);
        for (int other = 0; other < 256; ++other) mapping &= other == code || KeyState(scratch, other) == 0;
        states &= update(scratch, nullptr) && KeyState(scratch, code) == 2 && !pressed(scratch, nullptr, code) &&
                held(scratch, nullptr, code);
        states &= key(scan, true, true) && update(scratch, nullptr) && KeyState(scratch, code) == 2;
        states &= key(scan, false) && update(scratch, nullptr) && KeyState(scratch, code) == 0 && !pressed(scratch, nullptr, code) &&
                !held(scratch, nullptr, code);
    }
    bool tap = key(SDL_SCANCODE_A, true) && key(SDL_SCANCODE_A, false) && update(scratch, nullptr) && KeyState(scratch, kKeyA) == 1 &&
            update(scratch, nullptr) && KeyState(scratch, kKeyA) == 0;
    bool modifiers = key(SDL_SCANCODE_LCTRL, true) && key(SDL_SCANCODE_RALT, true) && key(SDL_SCANCODE_RSHIFT, true) &&
            update(scratch, nullptr) && KeyState(scratch, kKeyLeftCtrl) == 1 && KeyState(scratch, kKeyRightAlt) == 1 &&
            KeyState(scratch, kKeyRightShift) == 1;
    // A queued press before focus loss, a late press while unfocused, and a repeat after regain all stay released.
    key(SDL_SCANCODE_A, true);
    PushSdlFocus(false);
    key(SDL_SCANCODE_A, true);
    update(scratch, nullptr);
    bool focus = true;
    for (int code = 0; code < 256; ++code) focus &= KeyState(scratch, code) == 0;
    PushSdlFocus(true);
    key(SDL_SCANCODE_A, true, true);
    update(scratch, nullptr);
    focus &= KeyState(scratch, kKeyA) == 0;
    key(SDL_SCANCODE_A, true);
    update(scratch, nullptr);
    focus &= KeyState(scratch, kKeyA) == 1;
    reset();
    auto sdlFocus = [&](bool gain) {
        SDL_Event e{};
        e.type = gain ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
        e.window.windowID = platform::SdlGameWindowId();
        return SDL_PushEvent(&e);
    };
    key(SDL_SCANCODE_A, true);
    updateAll(input, nullptr);
    bool eventFocus = sdlFocus(false) && updateAll(input, nullptr) && KeyState(input, kKeyA) == 0;
    key(SDL_SCANCODE_Z, true); // Unfocused event, then regain and a fresh press in the same queue.
    eventFocus &= sdlFocus(true) && key(SDL_SCANCODE_A, true) && updateAll(input, nullptr) && KeyState(input, kKeyZ) == 0 &&
            KeyState(input, kKeyA) == 1;
    reset();
    key(SDL_SCANCODE_A, true, false, platform::SdlGameWindowId() + 1);
    key(SDL_SCANCODE_UNKNOWN, true);
    key(SDL_SCANCODE_F24, true); // No DIK slot; must not alias another key.
    update(scratch, nullptr);
    bool ignored = true;
    for (int code = 0; code < 256; ++code) ignored &= KeyState(scratch, code) == 0;
    reset();
    bool device = input && *reinterpret_cast<void**>(input + 4) == nullptr;
    OBL_LOG("autotest: %s keyboard-contract mapping=%d states=%d tap=%d modifiers=%d focus=%d sdl-focus=%d ignored=%d no-device=%d",
            mapping && states && tap && modifiers && focus && eventFocus && ignored && device ? "PASS" : "FAILED", mapping, states, tap,
            modifiers, focus, eventFocus, ignored, device);
}

void CheckSdlMouse(TestWindowHandle hwnd)
{
    if (!hwnd || !platform::SdlGameWindowId()) {
        OBL_LOG("autotest: FAILED sdl-mouse-contract needs SDL mouse and a wrapped window");
        return;
    }
    auto update = +[](uint8_t* input, void*) { return ui::InputUpdateMouse(*reinterpret_cast<ui::Input*>(input)); };
    auto updateAll = +[](uint8_t* input, void*) { return ui::InputUpdate(*reinterpret_cast<ui::Input*>(input)); };
    auto input = reinterpret_cast<uint8_t*>(game::GetInput());
    auto* renderer = game::GetRenderer();
    auto field = [&](InputInt member) -> int& { return InputOf(input).*member; };
    SIZE size{renderer->m_width, renderer->m_height};
    RECT image = platform::ClientImageRect(game::GetGameWindow(), size);
    POINT center{(image.left + image.right) / 2, (image.top + image.bottom) / 2};
    uint32_t window = platform::SdlGameWindowId();
    auto reset = [&] {
        PushSdlFocus(false);
        PushSdlFocus(true);
        platform::ClearSyntheticCursor();
    };
    auto motion = [&](float x, float y, uint32_t id) {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = id;
        e.motion.x = x;
        e.motion.y = y;
        return SDL_PushEvent(&e);
    };
    auto button = [&](int n, bool down, uint32_t id) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = id;
        e.button.button = static_cast<Uint8>(n);
        e.button.x = static_cast<float>(center.x);
        e.button.y = static_cast<float>(center.y);
        return SDL_PushEvent(&e);
    };
    auto wheel = [&](float y, bool flipped = false) {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.windowID = window;
        e.wheel.y = y;
        e.wheel.direction = flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
        return SDL_PushEvent(&e);
    };
    auto focus = [&](bool gain) {
        SDL_Event e{};
        e.type = gain ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
        e.window.windowID = window;
        return SDL_PushEvent(&e);
    };
    reset();
    bool position = motion(static_cast<float>(center.x), static_cast<float>(center.y), window) && update(input, nullptr) &&
            field(kMouseX) == size.cx / 2 && field(kMouseY) == size.cy / 2;
    position &= motion(static_cast<float>(image.right - 1), static_cast<float>(image.bottom - 1), window) && update(input, nullptr) &&
            field(kMouseX) == (image.right - 1 - image.left) * size.cx / (image.right - image.left) &&
            field(kMouseY) == (image.bottom - 1 - image.top) * size.cy / (image.bottom - image.top);
    bool bars = true;
    const POINT outside[] = {{image.left - 1, center.y}, {image.right, center.y}, {center.x, image.top - 1}, {center.x, image.bottom}};
    for (POINT p : outside)
        bars &= motion(static_cast<float>(p.x), static_cast<float>(p.y), window) && update(input, nullptr) && field(kMouseX) == -300 &&
                field(kMouseY) == -300;
    motion(static_cast<float>(center.x), static_cast<float>(center.y), window);
    update(input, nullptr);
    bool buttons = true, tap = true;
    for (int n : {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT}) {
        InputInt offset = n == SDL_BUTTON_LEFT ? kLeftButton : kRightButton;
        buttons &= button(n, true, window) && update(input, nullptr) && field(offset) == 1;
        buttons &= update(input, nullptr) && field(offset) == 2;
        buttons &= button(n, false, window) && update(input, nullptr) && field(offset) == 0;
        tap &= button(n, true, window) && button(n, false, window) && update(input, nullptr) && field(offset) == 1;
        tap &= update(input, nullptr) && field(offset) == 0;
    }
    bool wheels = wheel(1) && update(input, nullptr) && field(kWheel) == 120;
    wheels &= wheel(-2) && update(input, nullptr) && field(kWheel) == -240;
    wheels &= wheel(0.5f, true) && update(input, nullptr) && field(kWheel) == -60;
    // Each event is half a DirectInput unit, so conversion must keep the remainder.
    wheels &= wheel(1.0f / 240) && update(input, nullptr) && field(kWheel) == 0;
    wheels &= wheel(1.0f / 240) && update(input, nullptr) && field(kWheel) == 1;
    wheels &= update(input, nullptr) && field(kWheel) == 0;
    SDL_Event ignoredWheel{};
    ignoredWheel.type = SDL_EVENT_MOUSE_WHEEL;
    ignoredWheel.wheel.windowID = window + 1;
    ignoredWheel.wheel.y = 2;
    wheels &= SDL_PushEvent(&ignoredWheel) && update(input, nullptr) && field(kWheel) == 0;
    ignoredWheel.wheel.windowID = window;
    ignoredWheel.wheel.y = 0;
    ignoredWheel.wheel.x = 2;
    wheels &= SDL_PushEvent(&ignoredWheel) && update(input, nullptr) && field(kWheel) == 0;
#ifdef _WIN32
    // Through SDL's real Windows backend: the old WndProc path would double this to 240.
    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, 120), 0);
    bool single = update(input, nullptr) && field(kWheel) == 120 && field(kRecordCount) == 1;
    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(center.x, center.y));
    update(input, nullptr);
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(center.x, center.y));
    single &= update(input, nullptr) && field(kLeftButton) == 1 && field(kRecordCount) == 1;
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(center.x, center.y));
    single &= update(input, nullptr) && field(kLeftButton) == 0 && field(kRecordCount) == 1;
#else
    const bool single = true; // a check of SDL's Windows backend: there is no other native message path here
#endif
    motion(0, 0, window + 1);
    button(SDL_BUTTON_LEFT, true, window + 1);
    button(SDL_BUTTON_MIDDLE, true, window);
    bool ignored = update(input, nullptr) && field(kMouseX) == size.cx / 2 && field(kMouseY) == size.cy / 2 && field(kLeftButton) == 0 &&
            field(kRightButton) == 0 && field(kRecordCount) == 0;
    SDL_Event leave{};
    leave.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
    leave.window.windowID = window;
    bool leaves = SDL_PushEvent(&leave) && update(input, nullptr) && field(kMouseX) == -300 && field(kMouseY) == -300;
    leaves &= motion(static_cast<float>(center.x), static_cast<float>(center.y), window) && update(input, nullptr) &&
            field(kMouseX) == size.cx / 2 && field(kMouseY) == size.cy / 2;
    button(SDL_BUTTON_LEFT, true, window);
    update(input, nullptr);
    button(SDL_BUTTON_RIGHT, true, window);
    PushSdlFocus(false);
    button(SDL_BUTTON_LEFT, true, window);
    wheel(1);
    bool winFocus = updateAll(input, nullptr) && field(kLeftButton) == 0 && field(kRightButton) == 0 && field(kWheel) == 0;
    PushSdlFocus(true);
    winFocus &= updateAll(input, nullptr) && field(kLeftButton) == 0 && field(kRightButton) == 0;
    button(SDL_BUTTON_LEFT, true, window);
    bool eventFocus = focus(false) && button(SDL_BUTTON_RIGHT, true, window) && focus(true) &&
            motion(static_cast<float>(center.x), static_cast<float>(center.y), window) && updateAll(input, nullptr) &&
            field(kLeftButton) == 0 && field(kRightButton) == 0;
    eventFocus &= button(SDL_BUTTON_LEFT, true, window) && update(input, nullptr) && field(kLeftButton) == 1;
    reset();
    platform::SetSyntheticCursor(400, 300);
    OBL_LOG("autotest: %s sdl-mouse-contract position=%d bars=%d buttons=%d tap=%d wheel=%d single=%d ignored=%d leave=%d focus=%d "
            "sdl-focus=%d",
            position && bars && buttons && tap && wheels && single && ignored && leaves && winFocus && eventFocus ? "PASS" : "FAILED",
            position, bars, buttons, tap, wheels, single, ignored, leaves, winFocus, eventFocus);
}

} // namespace

bool ExecutePresenterCommand(const std::string& name, const std::vector<std::string>& args)
{
    auto* renderer = game::GetRenderer();
    TestWindowHandle hwnd = GetTestWindowHandle(renderer ? renderer->m_window : nullptr);
    if (name == "input-object-contract") {
        CheckInput();
        return true;
    }
    if (name == "sdl-mouse-contract") {
        CheckSdlMouse(hwnd);
        return true;
    }
    if (name == "keyboard-contract") {
        CheckSdlKeyboard(hwnd);
        return true;
    }
    if (name == "posted-mouse-burst") {
        if (!hwnd || args.size() != 1 || (args[0] != "0" && args[0] != "1")) {
            OBL_LOG("autotest: FAILED posted-mouse-burst needs down=0|1 and a window");
            return true;
        }
        bool down = args[0] == "1";
        bool posted = true;
        // Both sides of the button stress dispatch latency and eviction by irrelevant absolute axes.
#ifdef _WIN32
        for (int i = 0; i < 512; ++i) posted &= PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(300 + (i % 100), 300)) != FALSE;
        posted &= PostMessageW(hwnd, down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? MK_LBUTTON : 0, MAKELPARAM(400, 300)) != FALSE;
        for (int i = 0; i < 512; ++i)
            posted &= PostMessageW(hwnd, WM_MOUSEMOVE, down ? MK_LBUTTON : 0, MAKELPARAM(300 + (i % 100), 300)) != FALSE;
#else
        // Posted, not pushed: like the Windows messages above, the burst reaches the game at the next pump.
        for (int i = 0; i < 512; ++i) PostMouseMotion(static_cast<float>(300 + (i % 100)), 300, false);
        SDL_Event e{};
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = platform::SdlGameWindowId();
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = down;
        e.button.clicks = 1;
        e.button.x = 400;
        e.button.y = 300;
        platform::PostSdlEvent(e);
        for (int i = 0; i < 512; ++i) PostMouseMotion(static_cast<float>(300 + (i % 100)), 300, down);
#endif
        if (!posted) OBL_LOG("autotest: FAILED posted-mouse-burst PostMessage error");
        return true;
    }
    if (name == "posted-key") {
        // Posted like the keyboard driver does (scan code in lParam); the game ignores WM_KEYDOWN, SDL turns it into an event.
        int vk = args.size() > 0 ? std::strtol(args[0].c_str(), nullptr, 0) : 0;
        if (!hwnd || vk <= 0 || args.size() != 2 || (args[1] != "0" && args[1] != "1")) {
            OBL_LOG("autotest: FAILED posted-key needs a virtual-key code, down=0|1 and a window");
            return true;
        }
        bool down = args[1] == "1";
#ifndef _WIN32
        // SDL's own key event, as SDL's Windows backend makes it from the posted message.
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = platform::SdlGameWindowId();
        e.key.scancode = ScancodeOfVirtualKey(vk);
        e.key.down = down;
        if (e.key.scancode == SDL_SCANCODE_UNKNOWN)
            OBL_LOG("autotest: FAILED posted-key virtual-key code %d has no SDL scancode here", vk);
        else if (!SDL_PushEvent(&e))
            OBL_LOG("autotest: FAILED posted-key SDL_PushEvent error");
#else
        UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
        // Windows maps navigation VKs to their numpad scan byte even with MAPVK_VK_TO_VSC_EX.
        bool extended = (scan & 0xff00) != 0 || (vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE ||
                vk == VK_DIVIDE || vk == VK_NUMLOCK;
        LPARAM lParam = 1 | (static_cast<LPARAM>(scan & 0xff) << 16) | (extended ? (static_cast<LPARAM>(1) << 24) : 0) |
                (down ? 0 : (static_cast<LPARAM>(3) << 30));
        if (!PostMessageW(hwnd, down ? WM_KEYDOWN : WM_KEYUP, vk, lParam)) OBL_LOG("autotest: FAILED posted-key PostMessage error");
#endif
        return true;
    }
    if (name == "sdl-key") {
        // An SDL key event for the game window (a modifier cannot be posted: SDL asks the OS for its state), e.g. `sdl-key 225 1` = left Shift down.
        // An optional third number is the event's own modifier field (SDL_Keymod), e.g. `sdl-key 40 1 0x100` = Alt+Enter.
        int scancode = args.size() > 0 ? std::strtol(args[0].c_str(), nullptr, 0) : 0;
        if (scancode <= 0 || scancode >= SDL_SCANCODE_COUNT || (args.size() != 2 && args.size() != 3) ||
                (args[1] != "0" && args[1] != "1")) {
            OBL_LOG("autotest: FAILED sdl-key needs an SDL scancode, down=0|1 and optionally a modifier");
            return true;
        }
        SDL_Event e{};
        e.type = args[1] == "1" ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = platform::SdlGameWindowId();
        e.key.scancode = static_cast<SDL_Scancode>(scancode);
        e.key.down = args[1] == "1";
        if (args.size() == 3) e.key.mod = static_cast<SDL_Keymod>(std::strtol(args[2].c_str(), nullptr, 0));
        if (!SDL_PushEvent(&e)) OBL_LOG("autotest: FAILED sdl-key SDL_PushEvent error");
        return true;
    }
    if (name == "window-size") {
        int width = args.size() > 0 ? std::atoi(args[0].c_str()) : 0;
        int height = args.size() > 1 ? std::atoi(args[1].c_str()) : 0;
        if (!hwnd || width <= 0 || height <= 0) {
            OBL_LOG("autotest: FAILED window-size needs a window and positive dimensions");
            return true;
        }
#ifdef _WIN32
        RECT rect{0, 0, width, height};
        AdjustWindowRectEx(&rect, GetWindowLongW(hwnd, GWL_STYLE), FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE));
        SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
#else
        SDL_SetWindowSize(hwnd, width, height);
        SDL_SyncWindow(hwnd);
#endif
        return true;
    }
    if (name == "window-place") {
        // Optional <w> <h> place the window as if the game resolution were that size (e.g. beyond the monitor = borderless).
        if (!hwnd || (args.size() != 0 && args.size() != 2))
            OBL_LOG("autotest: FAILED window-place needs a window and either no arguments or <w> <h>");
        else
            platform::PlaceGameWindow(renderer->m_window, args.size() == 2 ? std::atoi(args[0].c_str()) : renderer->m_width,
                    args.size() == 2 ? std::atoi(args[1].c_str()) : renderer->m_height);
        return true;
    }
    if (name == "move-client") {
        // Besides numbers, each coordinate may be `center` or `max` (last client pixel) so scripts need no monitor size.
        RECT client{};
#ifdef _WIN32
        const bool sized = hwnd && GetClientRect(hwnd, &client);
#else
        int clientWidth = 0, clientHeight = 0;
        const bool sized = hwnd && SDL_GetWindowSize(hwnd, &clientWidth, &clientHeight);
        client.right = clientWidth;
        client.bottom = clientHeight;
#endif
        if (args.size() != 2 || !sized) {
            OBL_LOG("autotest: FAILED move-client needs x y and a window");
            return true;
        }
        auto resolve = [](const std::string& token, LONG size) {
            return token == "center" ? static_cast<int>(size / 2) : token == "max" ? static_cast<int>(size - 1) : std::atoi(token.c_str());
        };
        platform::SetSyntheticClientCursor(resolve(args[0], client.right), resolve(args[1], client.bottom));
        return true;
    }
    if (name == "sdl-relative-motion") {
        // What SDL's relative mode delivers: device units in xrel/yrel and a hidden position at the window corner.
        if (args.size() != 2 || !platform::SdlGameWindowId()) {
            OBL_LOG("autotest: FAILED sdl-relative-motion needs dx dy and a wrapped SDL window");
            return true;
        }
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = platform::SdlGameWindowId();
        e.motion.xrel = static_cast<float>(std::atof(args[0].c_str()));
        e.motion.yrel = static_cast<float>(std::atof(args[1].c_str()));
        if (!SDL_PushEvent(&e)) OBL_LOG("autotest: FAILED sdl-relative-motion SDL_PushEvent: %s", SDL_GetError());
        return true;
    }
    if (name == "relative-motion") {
        if (args.size() != 2)
            OBL_LOG("autotest: FAILED relative-motion needs dx dy");
        else
            platform::InjectRelativeMotion(std::atoi(args[0].c_str()), std::atoi(args[1].c_str()));
        return true;
    }
    if (name == "input-focus") {
        if (!hwnd || args.size() != 1 || (args[0] != "0" && args[0] != "1"))
            OBL_LOG("autotest: FAILED input-focus needs 0|1 and a window");
        else {
            // A pushed SDL focus event exercises the focus cleanup without changing the user's foreground window.
            PushSdlFocus(args[0] == "1");
        }
        return true;
    }
    if (name != "sdl-verify") return false;
    platform::RequestSdlPresentVerify();
    OBL_LOG("autotest: sdl-verify requested (verified so far: %u)", platform::GetSdlPresenterStats().verifications);
    return true;
}

bool ReadPresenterProbe(const std::string& name, double& out)
{
    const auto& s = platform::GetSdlPresenterStats();
    if (name == "sdl-active")
        out = s.active ? 1 : 0;
    else if (name == "sdl-relative-requested")
        out = platform::GetSdlRelativeDragState().requested;
    else if (name == "audio-suspends") // long frames that suspended the audio output (audio_output.cpp)
        out = platform::GetAudioOutputStats().suspends;
    else if (name == "audio-suspended")
        out = platform::GetAudioOutputStats().suspended ? 1 : 0;
    else if (name == "mouse-confine") // [Display] ConfineMouse holds the pointer now (a hidden run never does)
        out = platform::MouseConfineWanted() ? 1 : 0;
    else if (name == "keyboard-device") {
        auto input = reinterpret_cast<uint8_t*>(game::GetInput());
        out = input && *reinterpret_cast<void**>(input + 4) != nullptr;
    } else if (name.starts_with("key-")) {
        char* end;
        long code = std::strtol(name.c_str() + 4, &end, 0);
        auto input = reinterpret_cast<uint8_t*>(game::GetInput());
        if (!input || *end || end == name.c_str() + 4 || code < 0 || code > 255) return false;
        out = KeyState(input, code);
    } else if (name == "sdl-wrapped")
        out = s.wrapped ? 1 : 0;
    else if (name == "sdl-created")
        out = s.created ? 1 : 0;
    else if (name == "sdl-software") // no GPU renderer: SDL's software renderer presents
        out = s.software ? 1 : 0;
    else if (name == "sdl-window-width")
        out = s.windowWidth;
    else if (name == "sdl-window-height")
        out = s.windowHeight;
    else if (name == "sdl-keyboard-events")
        out = static_cast<double>(platform::GetSdlEventCounts().keyboard);
    else if (name == "sdl-mouse-motion-events")
        out = static_cast<double>(platform::GetSdlEventCounts().mouseMotion);
    else if (name == "sdl-mouse-button-events")
        out = static_cast<double>(platform::GetSdlEventCounts().mouseButtons);
    else if (name == "sdl-other-events")
        out = static_cast<double>(platform::GetSdlEventCounts().other);
    else if (name == "sdl-presents")
        out = static_cast<double>(s.presents);
    else if (name == "sdl-texture-width")
        out = s.textureWidth;
    else if (name == "sdl-texture-height")
        out = s.textureHeight;
    else if (name == "sdl-verified")
        out = s.verifications;
    else if (name == "sdl-mismatches")
        out = static_cast<double>(s.lastMismatches);
    else if (name == "sdl-pixels")
        out = static_cast<double>(s.lastPixels);
    else if (name == "sdl-bar-pixels")
        out = static_cast<double>(s.lastBarPixels);
    else if (name == "sdl-bar-mismatches")
        out = static_cast<double>(s.lastBarMismatches);
    else if (name == "display-scale")
        out = config::Get().scale;
    else if (name == "mouse-x" || name == "mouse-y" || name == "mouse-dx" || name == "mouse-dy" || name == "mouse-relative" ||
            name == "mouse-left") {
        auto input = reinterpret_cast<uint8_t*>(game::GetInput());
        if (!input) return false;
        if (name == "mouse-relative")
            out = InputOf(input).m_relativeMode;
        else
            out = InputOf(input).*
                    ((name == "mouse-left"               ? kLeftButton
                                    : name == "mouse-x"  ? kMouseX
                                    : name == "mouse-y"  ? kMouseY
                                    : name == "mouse-dx" ? kDeltaX
                                                         : kDeltaY));
    } else if (name == "camera-drag" || name == "camera-speed" || name == "camera-vx" || name == "camera-vy") {
        const auto* world = game::GetWorld();
        auto map = ui::GetMapView();
        if (!world || !map) return false;
        out = name == "camera-vx"       ? map->m_velocityX
                : name == "camera-vy"   ? map->m_velocityY
                : name == "camera-drag" ? game::GetContext().view.m_cameraDragging
                                        : std::abs(map->m_velocityX) + std::abs(map->m_velocityY);
    } else {
        auto* renderer = game::GetRenderer();
        TestWindowHandle hwnd = GetTestWindowHandle(renderer ? renderer->m_window : nullptr);
        if (!hwnd) return false;
#ifndef _WIN32
        // Without Win32 only the client size, the title and the windowed flags; the monitor and style probes are Win32's.
        auto input = reinterpret_cast<uint8_t*>(game::GetInput());
        int width = 0, height = 0;
        SDL_GetWindowSize(hwnd, &width, &height);
        if (name == "window-width")
            out = width;
        else if (name == "window-height")
            out = height;
        else if (name == "window-title-game")
            out = std::strcmp(SDL_GetWindowTitle(hwnd), "Beach Life") == 0;
        else if (name == "flag-global-windowed")
            out = game::GetWindowedMode();
        else if (name == "flag-renderer-windowed")
            out = renderer->m_windowed;
        else if (name == "flag-input-windowed" && input)
            out = InputOf(input).m_windowed;
        else
            return false;
#else
        RECT client{};
        GetClientRect(hwnd, &client);
        POINT origin{0, 0};
        ClientToScreen(hwnd, &origin);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        auto input = reinterpret_cast<uint8_t*>(game::GetInput());
        if (name == "monitor-width")
            out = mi.rcMonitor.right - mi.rcMonitor.left;
        else if (name == "monitor-height")
            out = mi.rcMonitor.bottom - mi.rcMonitor.top;
        else if (name == "client-offset-x") // client origin relative to the monitor origin
            out = origin.x - mi.rcMonitor.left;
        else if (name == "client-offset-y")
            out = origin.y - mi.rcMonitor.top;
        else if (name == "client-is-monitor")
            out = origin.x == mi.rcMonitor.left && origin.y == mi.rcMonitor.top && client.right == mi.rcMonitor.right - mi.rcMonitor.left &&
                    client.bottom == mi.rcMonitor.bottom - mi.rcMonitor.top;
        else if (name == "window-popup") // WS_POPUP without caption/frame
            out = (GetWindowLongW(hwnd, GWL_STYLE) & (WS_POPUP | WS_CAPTION | WS_THICKFRAME)) == WS_POPUP;
        else if (name == "window-capture") // the game window holds the mouse capture
            out = GetCapture() == hwnd;
        else if (name == "window-topmost")
            out = (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        else if (name == "window-class-game" || name == "window-title-game") { // "Beach Life", as before
            wchar_t text[64]{};
            if (name == "window-class-game")
                GetClassNameW(hwnd, text, 64);
            else
                GetWindowTextW(hwnd, text, 64);
            out = std::wcscmp(text, L"Beach Life") == 0;
        } else if (name == "window-icon-game") { // SDL set the OpenBeach icon as the window's big and small icon
            const bool big = SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) != 0;
            const bool compactIcon = SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0) != 0;
            out = platform::GetSdlPresenterStats().icon && big && compactIcon;
        } else if (name == "window-proc-sdl") // SDL's class procedure, not subclassed
            out = GetWindowLongPtrW(hwnd, GWLP_WNDPROC) == GetClassLongPtrW(hwnd, GCLP_WNDPROC);
        else if (name == "flag-global-windowed")
            out = game::GetWindowedMode();
        else if (name == "flag-renderer-windowed")
            out = renderer->m_windowed;
        else if (name == "flag-input-windowed" && input)
            out = InputOf(input).m_windowed;
        else if (name == "window-width")
            out = client.right;
        else if (name == "window-height")
            out = client.bottom;
        else
            return false;
#endif
    }
    return true;
}

} // namespace obl::test
