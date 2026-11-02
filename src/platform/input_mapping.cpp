#include "input_mapping.h"

#include <SDL3/SDL_scancode.h>

namespace obl::platform {

namespace {

// SDL scancodes describe physical keys; the game uses stable PC scan-set indices.
// Keycodes and modifier masks depend on the keyboard layout and cannot index that map.
constexpr auto kSdlToKeyCode = [] {
    std::array<input::KeyCode, SDL_SCANCODE_COUNT> map{};
    constexpr input::KeyCode letters[] = {input::KeyCode::A, input::KeyCode::B, input::KeyCode::C, input::KeyCode::D, input::KeyCode::E,
            input::KeyCode::F, input::KeyCode::G, input::KeyCode::H, input::KeyCode::I, input::KeyCode::J, input::KeyCode::K,
            input::KeyCode::L, input::KeyCode::M, input::KeyCode::N, input::KeyCode::O, input::KeyCode::P, input::KeyCode::Q,
            input::KeyCode::R, input::KeyCode::S, input::KeyCode::T, input::KeyCode::U, input::KeyCode::V, input::KeyCode::W,
            input::KeyCode::X, input::KeyCode::Y, input::KeyCode::Z};
    for (int i = 0; i < 26; ++i) map[SDL_SCANCODE_A + i] = letters[i];
    for (int i = 0; i < 9; ++i) map[SDL_SCANCODE_1 + i] = static_cast<input::KeyCode>(input::KeyCode::Digit1 + i);
    map[SDL_SCANCODE_0] = input::KeyCode::Digit0;
    for (int i = 0; i < 10; ++i) map[SDL_SCANCODE_F1 + i] = static_cast<input::KeyCode>(input::KeyCode::F1 + i);
    map[SDL_SCANCODE_F11] = input::KeyCode::F11;
    map[SDL_SCANCODE_F12] = input::KeyCode::F12;
    map[SDL_SCANCODE_F13] = input::KeyCode::F13;
    map[SDL_SCANCODE_F14] = input::KeyCode::F14;
    map[SDL_SCANCODE_F15] = input::KeyCode::F15;
    map[SDL_SCANCODE_ESCAPE] = input::KeyCode::Escape;
    map[SDL_SCANCODE_RETURN] = input::KeyCode::Enter;
    map[SDL_SCANCODE_BACKSPACE] = input::KeyCode::Backspace;
    map[SDL_SCANCODE_TAB] = input::KeyCode::Tab;
    map[SDL_SCANCODE_SPACE] = input::KeyCode::Space;
    map[SDL_SCANCODE_MINUS] = input::KeyCode::Minus;
    map[SDL_SCANCODE_EQUALS] = input::KeyCode::Equals;
    map[SDL_SCANCODE_LEFTBRACKET] = input::KeyCode::LeftBracket;
    map[SDL_SCANCODE_RIGHTBRACKET] = input::KeyCode::RightBracket;
    map[SDL_SCANCODE_BACKSLASH] = input::KeyCode::Backslash;
    map[SDL_SCANCODE_NONUSHASH] = input::KeyCode::Backslash;
    map[SDL_SCANCODE_NONUSBACKSLASH] = input::KeyCode::NonUsBackslash;
    map[SDL_SCANCODE_SEMICOLON] = input::KeyCode::Semicolon;
    map[SDL_SCANCODE_APOSTROPHE] = input::KeyCode::Apostrophe;
    map[SDL_SCANCODE_GRAVE] = input::KeyCode::Grave;
    map[SDL_SCANCODE_COMMA] = input::KeyCode::Comma;
    map[SDL_SCANCODE_PERIOD] = input::KeyCode::Period;
    map[SDL_SCANCODE_SLASH] = input::KeyCode::Slash;
    map[SDL_SCANCODE_CAPSLOCK] = input::KeyCode::CapsLock;
    map[SDL_SCANCODE_PRINTSCREEN] = input::KeyCode::PrintScreen;
    map[SDL_SCANCODE_SCROLLLOCK] = input::KeyCode::ScrollLock;
    map[SDL_SCANCODE_PAUSE] = input::KeyCode::Pause;
    map[SDL_SCANCODE_INSERT] = input::KeyCode::Insert;
    map[SDL_SCANCODE_HOME] = input::KeyCode::Home;
    map[SDL_SCANCODE_PAGEUP] = input::KeyCode::PageUp;
    map[SDL_SCANCODE_DELETE] = input::KeyCode::Delete;
    map[SDL_SCANCODE_END] = input::KeyCode::End;
    map[SDL_SCANCODE_PAGEDOWN] = input::KeyCode::PageDown;
    map[SDL_SCANCODE_LEFT] = input::KeyCode::Left;
    map[SDL_SCANCODE_RIGHT] = input::KeyCode::Right;
    map[SDL_SCANCODE_UP] = input::KeyCode::Up;
    map[SDL_SCANCODE_DOWN] = input::KeyCode::Down;
    map[SDL_SCANCODE_NUMLOCKCLEAR] = input::KeyCode::NumLock;
    map[SDL_SCANCODE_KP_DIVIDE] = input::KeyCode::KeypadDivide;
    map[SDL_SCANCODE_KP_MULTIPLY] = input::KeyCode::KeypadMultiply;
    map[SDL_SCANCODE_KP_MINUS] = input::KeyCode::KeypadMinus;
    map[SDL_SCANCODE_KP_PLUS] = input::KeyCode::KeypadPlus;
    map[SDL_SCANCODE_KP_ENTER] = input::KeyCode::KeypadEnter;
    map[SDL_SCANCODE_KP_0] = input::KeyCode::Keypad0;
    map[SDL_SCANCODE_KP_1] = input::KeyCode::Keypad1;
    map[SDL_SCANCODE_KP_2] = input::KeyCode::Keypad2;
    map[SDL_SCANCODE_KP_3] = input::KeyCode::Keypad3;
    map[SDL_SCANCODE_KP_4] = input::KeyCode::Keypad4;
    map[SDL_SCANCODE_KP_5] = input::KeyCode::Keypad5;
    map[SDL_SCANCODE_KP_6] = input::KeyCode::Keypad6;
    map[SDL_SCANCODE_KP_7] = input::KeyCode::Keypad7;
    map[SDL_SCANCODE_KP_8] = input::KeyCode::Keypad8;
    map[SDL_SCANCODE_KP_9] = input::KeyCode::Keypad9;
    map[SDL_SCANCODE_KP_PERIOD] = input::KeyCode::KeypadPeriod;
    map[SDL_SCANCODE_LCTRL] = input::KeyCode::LeftControl;
    map[SDL_SCANCODE_RCTRL] = input::KeyCode::RightControl;
    map[SDL_SCANCODE_LSHIFT] = input::KeyCode::LeftShift;
    map[SDL_SCANCODE_RSHIFT] = input::KeyCode::RightShift;
    map[SDL_SCANCODE_LALT] = input::KeyCode::LeftAlt;
    map[SDL_SCANCODE_RALT] = input::KeyCode::RightAlt;
    map[SDL_SCANCODE_LGUI] = input::KeyCode::LeftGui;
    map[SDL_SCANCODE_RGUI] = input::KeyCode::RightGui;
    map[SDL_SCANCODE_APPLICATION] = input::KeyCode::Application;
    map[SDL_SCANCODE_INTERNATIONAL1] = input::KeyCode::International1;
    map[SDL_SCANCODE_INTERNATIONAL2] = input::KeyCode::International2;
    map[SDL_SCANCODE_INTERNATIONAL3] = input::KeyCode::International3;
    map[SDL_SCANCODE_INTERNATIONAL4] = input::KeyCode::International4;
    map[SDL_SCANCODE_INTERNATIONAL5] = input::KeyCode::International5;
    map[SDL_SCANCODE_KP_COMMA] = input::KeyCode::KeypadComma;
    map[SDL_SCANCODE_KP_EQUALS] = input::KeyCode::KeypadEquals;
    map[SDL_SCANCODE_INTERNATIONAL6] = input::KeyCode::International6;
    map[SDL_SCANCODE_MUTE] = input::KeyCode::Mute;
    map[SDL_SCANCODE_VOLUMEDOWN] = input::KeyCode::VolumeDown;
    map[SDL_SCANCODE_VOLUMEUP] = input::KeyCode::VolumeUp;
    map[SDL_SCANCODE_MEDIA_NEXT_TRACK] = input::KeyCode::MediaNextTrack;
    map[SDL_SCANCODE_MEDIA_PREVIOUS_TRACK] = input::KeyCode::MediaPreviousTrack;
    map[SDL_SCANCODE_MEDIA_STOP] = input::KeyCode::MediaStop;
    map[SDL_SCANCODE_MEDIA_PLAY_PAUSE] = input::KeyCode::MediaPlayPause;
    map[SDL_SCANCODE_MEDIA_SELECT] = input::KeyCode::MediaSelect;
    map[SDL_SCANCODE_AC_SEARCH] = input::KeyCode::BrowserSearch;
    map[SDL_SCANCODE_AC_HOME] = input::KeyCode::BrowserHome;
    map[SDL_SCANCODE_AC_BACK] = input::KeyCode::BrowserBack;
    map[SDL_SCANCODE_AC_FORWARD] = input::KeyCode::BrowserForward;
    map[SDL_SCANCODE_AC_STOP] = input::KeyCode::BrowserStop;
    map[SDL_SCANCODE_AC_REFRESH] = input::KeyCode::BrowserRefresh;
    map[SDL_SCANCODE_AC_BOOKMARKS] = input::KeyCode::BrowserBookmarks;
    map[SDL_SCANCODE_SLEEP] = input::KeyCode::Sleep;
    return map;
}();

} // namespace

input::KeyCode SdlScancodeToKeyCode(int scancode)
{
    return scancode > 0 && scancode < SDL_SCANCODE_COUNT ? kSdlToKeyCode[scancode] : input::KeyCode::None;
}

void KeyLatch::Reset()
{
    m_down.fill(false);
    m_pressed.fill(false);
}

void KeyLatch::OnKey(input::KeyCode code, bool down, bool repeat)
{
    if (!code || repeat) return;
    if (down && !m_down[code]) m_pressed[code] = true;
    m_down[code] = down;
}

void KeyLatch::Commit(uint8_t* states)
{
    for (int code = 0; code < 256; ++code) {
        uint8_t& state = states[code];
        // Preserve a complete down/up between frames as one pressed frame.
        state = m_pressed[code] ? 1 : m_down[code] ? (state ? 2 : 1) : 0;
    }
    m_pressed.fill(false);
}

int UnitAccumulator::Add(double units)
{
    m_remainder += units;
    int whole = static_cast<int>(m_remainder);
    m_remainder -= whole;
    return whole;
}

double SdlWheelUnits(float notches, bool flipped)
{
    double units = notches * 120.0;
    return flipped ? -units : units;
}

} // namespace obl::platform
