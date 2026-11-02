#pragma once
// Pure logic of the game's software cursor (cursor.h), checked by tests/unit/cursor_logic_test.cpp.
#include <cstdint>
#include <limits>

namespace obl::cursor {

constexpr int kOutside = -300; // Input position while the pointer is outside the window
constexpr int kStartX = 400;
constexpr int kStartY = 300;
constexpr float kStartSpeed = 10.0f;
constexpr double kCellSize = 32;
constexpr int kShapes = 8; // CursorRender's legal shape/frame range

// CursorUpdate: the add is done in double and rounded to float after the add and after every subtraction of 8.0.
inline float AdvancePhase(float phase, float delta, float speed)
{
    float value = static_cast<float>(static_cast<double>(delta) * speed + phase);
    while (8.0f <= value) value = static_cast<float>(static_cast<double>(value) - 8.0);
    return value;
}

// CursorIsInRect(left, top, width, height): the box is closed on all four sides.
inline bool InRect(int x, int y, int left, int top, int width, int height)
{
    return x >= left && x <= left + width && y >= top && y <= top + height;
}

// Truncation of a divide result toward zero; NaN and values outside int give 0x80000000.
inline int Truncate(double value)
{
    if (!(value > -2147483649.0 && value < 2147483648.0)) return std::numeric_limits<int>::min();
    return static_cast<int>(value);
}

// CursorGetPos: the raw position divided by the renderer zoom scale (2.0, 1.0, 0.5).
inline void WorldPos(int x, int y, float zoom, int out[2])
{
    out[0] = Truncate(static_cast<double>(x) / zoom);
    out[1] = Truncate(static_cast<double>(y) / zoom);
}

// CursorSetShape forwards the shape to InputSetCursorShape as shape + 1 for 0..6 and leaves the Input shape alone otherwise.
inline int InputShape(int shape)
{
    return shape >= 0 && shape <= 6 ? shape + 1 : 0;
}

struct SourceRect {
    int left, top, right, bottom;
};
inline SourceRect RenderCell(int shape, int frame)
{
    const int left = shape * 32, top = frame * 32;
    return {left, top, left + 31, top + 31};
}
inline bool RenderStateLegal(int shape, int frame)
{
    return shape < kShapes && frame < kShapes;
}
inline bool Visible(int x, int y)
{
    return x != kOutside || y != kOutside;
}

} // namespace obl::cursor
