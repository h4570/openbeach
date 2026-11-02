#pragma once

#include "core/rect.h"
#include <cstdint>

// Native input records, read directly by the input and platform code.
namespace obl::ui {

struct Input {
    void* m_directInput;
    void* m_keyboardDevice; // IDirectInputDevice; null on the SDL keyboard path
    void* m_mouseDevice;
    void* m_window; // InputInit argument (opaque platform window handle)
    void* m_module; // InputInit argument (opaque module handle), used to load the cursor resources
    int m_width;
    int m_height;
    uint8_t m_windowed; // InputConstruct argument; the cursor lock is converted to screen space
    uint8_t m_keys[256]; // by input::KeyCode: 0 released, 1 pressed this update, 2 held
    int m_leftButton;
    int m_rightButton;
    int m_deltaX; // relative deltas of this update
    int m_deltaY;
    int m_wheel;
    int m_x;
    int m_y;
    uint8_t m_mouseStateValid; // Set when GetObjectInfo for the wheel succeeds
    uint8_t m_systemCursorEnabled;
    int m_cursorShape; // InputSetCursorShape: current shape id

    uint8_t m_relativeMode; // InputSetRelativeMode: camera drag
    struct MouseRecord {
        uint32_t offset, data, timestamp, sequence, appData;
    };
    MouseRecord m_records[60];
    int m_recordCount;
    uint8_t m_deferredLeftRelease;
    uint8_t m_deferredRightRelease;
};

} // namespace obl::ui
