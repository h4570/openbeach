#pragma once

#include <cstdint>

// Native cursor state, read directly by cursor and view code.
namespace obl::ui {

struct SoftwareCursor {
    void* m_owner; // CursorConstruct third argument (the window), stored untouched
    void* m_image; // cursor sprite sheet loaded from its TGA file, 32x32 cells: column = shape, row = frame
    int m_x; // last Input position, -300/-300 outside the window
    int m_y;
    int m_shape; // CursorSetShape argument (0 arrow ... 6); CursorRender accepts 0..7
    int m_frame; // animation row; CursorUpdate clears it
    float m_phase; // animation clock, wraps below 8.0
    float m_speed; // phase per second, 10.0
    uint8_t m_systemCursorHidden; // CursorSetSystemHidden argument
    uint8_t m_dragMode; // CursorSetDragMode argument (camera drag, relative mouse)
};
// The cursor world and view code read (only the position) is the software cursor itself.
using Cursor = SoftwareCursor;

} // namespace obl::ui
