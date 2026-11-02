#pragma once

#include "game/ui/input_types.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

// The pure state of the Input object (docs/architecture/input.md): what the Input entry points (input.h) do to its fields, independent of where
// keys, buttons and the cursor position come from. src/game/ui/input_ops.cpp drives it for both hosts; the device side is an
// InputBackend (SDL on Windows, none headless).
namespace obl::input_state {

// The position the game reads while the cursor is outside the window (or there is no cursor).
constexpr int kNoCursor = -300;

struct Point {
    int x = kNoCursor;
    int y = kNoCursor;
};

// Offsets of the five channels in the game's buffered mouse records (DirectInput's DIMOFS_X/Y/Z/BUTTON0/BUTTON1).
constexpr uint32_t kRecordX = 0;
constexpr uint32_t kRecordY = 4;
constexpr uint32_t kRecordWheel = 8;
constexpr uint32_t kRecordLeft = 12;
constexpr uint32_t kRecordRight = 13;
constexpr uint32_t kButtonDown = 0x80;

// InputConstruct: the complete state is cleared, including fields the game leaves until InitMouse.
inline void Construct(ui::Input& input, int width, int height, int windowed)
{
    input = {};
    input.m_width = width;
    input.m_height = height;
    input.m_windowed = static_cast<uint8_t>(windowed);
}

// The mouse part of InputInit, after the device: a valid wheel, the system cursor on, nothing buffered, no drag, shape 0.
inline void InitMouse(ui::Input& input)
{
    input.m_mouseStateValid = 1;
    input.m_systemCursorEnabled = 1;
    input.m_recordCount = 0;
    input.m_deferredLeftRelease = input.m_deferredRightRelease = 0;
    input.m_relativeMode = 0;
    input.m_cursorShape = 0;
}

// What a reset (focus loss, an update without devices, the destructor) leaves: no key, no button, no motion, no cursor.
inline void Reset(ui::Input& input)
{
    std::memset(input.m_keys, 0, sizeof(input.m_keys)); // the game key-state map: 0 released, 1 pressed, 2 held
    input.m_leftButton = input.m_rightButton = 0;
    input.m_deltaX = input.m_deltaY = input.m_wheel = 0;
    input.m_deferredLeftRelease = input.m_deferredRightRelease = 0;
    input.m_x = kNoCursor;
    input.m_y = kNoCursor;
}

inline bool KeyHeld(const ui::Input& input, int code)
{
    return code >= 0 && code < 256 && input.m_keys[code] == 2;
}
inline bool KeyPressed(const ui::Input& input, int code)
{
    return code >= 0 && code < 256 && input.m_keys[code] == 1;
}

// InputUpdateMouse after a successful read of `count` records into m_records, with the cursor at `pos` (game coordinates; the
// drag anchor while the camera drag runs). A button is 1 in the update it went down, then 2 while held; a release in the same
// update as the press is deferred to the next one so the game sees the click.
inline void ApplyMouse(ui::Input& input, Point pos, int count)
{
    input.m_deltaX = pos.x - input.m_x;
    input.m_deltaY = pos.y - input.m_y;
    input.m_x = pos.x;
    input.m_y = pos.y;
    if (input.m_windowed) {
        if (pos.x < 0 || pos.y < 0 || pos.x > input.m_width || pos.y > input.m_height) input.m_x = input.m_y = kNoCursor;
    } else {
        input.m_x = std::clamp(input.m_x, 0, std::max(0, input.m_width - 1));
        input.m_y = std::clamp(input.m_y, 0, std::max(0, input.m_height - 1));
    }
    auto advance = [](int& button, uint8_t& release) {
        if (button == 1) button = 2;
        if (release) {
            button = 0;
            release = 0;
        }
    };
    advance(input.m_leftButton, input.m_deferredLeftRelease);
    advance(input.m_rightButton, input.m_deferredRightRelease);
    if (input.m_relativeMode) input.m_deltaX = input.m_deltaY = 0;
    if (input.m_x == kNoCursor || input.m_y == kNoCursor) return;
    auto buttonEvent = [](int& button, uint8_t& release, uint32_t data) {
        if (data & kButtonDown) {
            if (!button) button = 1;
        } else if (button == 1)
            release = 1;
        else
            button = 0;
    };
    for (int i = 0; i < count; ++i) {
        const auto& event = input.m_records[i];
        switch (event.offset) {
        case kRecordX:
            if (input.m_relativeMode) input.m_deltaX += static_cast<int>(event.data);
            break;
        case kRecordY:
            if (input.m_relativeMode) input.m_deltaY += static_cast<int>(event.data);
            break;
        case kRecordWheel: input.m_wheel += static_cast<int>(event.data); break;
        case kRecordLeft: buttonEvent(input.m_leftButton, input.m_deferredLeftRelease, event.data); break;
        case kRecordRight: buttonEvent(input.m_rightButton, input.m_deferredRightRelease, event.data); break;
        }
    }
}

} // namespace obl::input_state
