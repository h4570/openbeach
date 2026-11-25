#include "core/key_names.h"

#include "core/key_code.h"

#include <array>
#include <utility>

namespace obl::input {

std::string_view KeyName(uint8_t code)
{
    // One row per enumerator of input::KeyCode; key_names_test checks that none is missing.
    static constexpr std::pair<uint8_t, std::string_view> kNames[] = {
            {input::Escape, "Escape"},
            {input::Digit1, "Digit1"},
            {input::Digit2, "Digit2"},
            {input::Digit3, "Digit3"},
            {input::Digit4, "Digit4"},
            {input::Digit5, "Digit5"},
            {input::Digit6, "Digit6"},
            {input::Digit7, "Digit7"},
            {input::Digit8, "Digit8"},
            {input::Digit9, "Digit9"},
            {input::Digit0, "Digit0"},
            {input::Minus, "Minus"},
            {input::Equals, "Equals"},
            {input::Backspace, "Backspace"},
            {input::Tab, "Tab"},
            {input::Q, "Q"},
            {input::W, "W"},
            {input::E, "E"},
            {input::R, "R"},
            {input::T, "T"},
            {input::Y, "Y"},
            {input::U, "U"},
            {input::I, "I"},
            {input::O, "O"},
            {input::P, "P"},
            {input::LeftBracket, "LeftBracket"},
            {input::RightBracket, "RightBracket"},
            {input::Enter, "Enter"},
            {input::LeftControl, "LeftControl"},
            {input::A, "A"},
            {input::S, "S"},
            {input::D, "D"},
            {input::F, "F"},
            {input::G, "G"},
            {input::H, "H"},
            {input::J, "J"},
            {input::K, "K"},
            {input::L, "L"},
            {input::Semicolon, "Semicolon"},
            {input::Apostrophe, "Apostrophe"},
            {input::Grave, "Grave"},
            {input::LeftShift, "LeftShift"},
            {input::Backslash, "Backslash"},
            {input::Z, "Z"},
            {input::X, "X"},
            {input::C, "C"},
            {input::V, "V"},
            {input::B, "B"},
            {input::N, "N"},
            {input::M, "M"},
            {input::Comma, "Comma"},
            {input::Period, "Period"},
            {input::Slash, "Slash"},
            {input::RightShift, "RightShift"},
            {input::KeypadMultiply, "KeypadMultiply"},
            {input::LeftAlt, "LeftAlt"},
            {input::Space, "Space"},
            {input::CapsLock, "CapsLock"},
            {input::F1, "F1"},
            {input::F2, "F2"},
            {input::F3, "F3"},
            {input::F4, "F4"},
            {input::F5, "F5"},
            {input::F6, "F6"},
            {input::F7, "F7"},
            {input::F8, "F8"},
            {input::F9, "F9"},
            {input::F10, "F10"},
            {input::NumLock, "NumLock"},
            {input::ScrollLock, "ScrollLock"},
            {input::Keypad7, "Keypad7"},
            {input::Keypad8, "Keypad8"},
            {input::Keypad9, "Keypad9"},
            {input::KeypadMinus, "KeypadMinus"},
            {input::Keypad4, "Keypad4"},
            {input::Keypad5, "Keypad5"},
            {input::Keypad6, "Keypad6"},
            {input::KeypadPlus, "KeypadPlus"},
            {input::Keypad1, "Keypad1"},
            {input::Keypad2, "Keypad2"},
            {input::Keypad3, "Keypad3"},
            {input::Keypad0, "Keypad0"},
            {input::KeypadPeriod, "KeypadPeriod"},
            {input::NonUsBackslash, "NonUsBackslash"},
            {input::F11, "F11"},
            {input::F12, "F12"},
            {input::KeypadEquals, "KeypadEquals"},
            {input::International6, "International6"},
            {input::F13, "F13"},
            {input::F14, "F14"},
            {input::F15, "F15"},
            {input::International2, "International2"},
            {input::International1, "International1"},
            {input::International4, "International4"},
            {input::International5, "International5"},
            {input::International3, "International3"},
            {input::KeypadComma, "KeypadComma"},
            {input::MediaPreviousTrack, "MediaPreviousTrack"},
            {input::MediaNextTrack, "MediaNextTrack"},
            {input::KeypadEnter, "KeypadEnter"},
            {input::RightControl, "RightControl"},
            {input::Mute, "Mute"},
            {input::MediaPlayPause, "MediaPlayPause"},
            {input::MediaStop, "MediaStop"},
            {input::VolumeDown, "VolumeDown"},
            {input::VolumeUp, "VolumeUp"},
            {input::BrowserHome, "BrowserHome"},
            {input::KeypadDivide, "KeypadDivide"},
            {input::PrintScreen, "PrintScreen"},
            {input::RightAlt, "RightAlt"},
            {input::Pause, "Pause"},
            {input::Home, "Home"},
            {input::Up, "Up"},
            {input::PageUp, "PageUp"},
            {input::Left, "Left"},
            {input::Right, "Right"},
            {input::End, "End"},
            {input::Down, "Down"},
            {input::PageDown, "PageDown"},
            {input::Insert, "Insert"},
            {input::Delete, "Delete"},
            {input::LeftGui, "LeftGui"},
            {input::RightGui, "RightGui"},
            {input::Application, "Application"},
            {input::Sleep, "Sleep"},
            {input::BrowserSearch, "BrowserSearch"},
            {input::BrowserBookmarks, "BrowserBookmarks"},
            {input::BrowserRefresh, "BrowserRefresh"},
            {input::BrowserStop, "BrowserStop"},
            {input::BrowserForward, "BrowserForward"},
            {input::BrowserBack, "BrowserBack"},
            {input::MediaSelect, "MediaSelect"},
    };
    static const std::array<std::string_view, 256> table = [] {
        std::array<std::string_view, 256> names{};
        for (const auto& [key, name] : kNames) names[key] = name;
        return names;
    }();
    return table[code];
}

uint8_t FindKey(std::string_view name)
{
    const auto lower = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); };
    for (int code = 1; code < 256; ++code) {
        const std::string_view known = KeyName(static_cast<uint8_t>(code));
        if (known.size() != name.size() || known.empty()) continue;
        bool same = true;
        for (size_t i = 0; i < known.size() && same; ++i) same = lower(known[i]) == lower(name[i]);
        if (same) return static_cast<uint8_t>(code);
    }
    return 0;
}

} // namespace obl::input
