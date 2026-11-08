#pragma once

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "core/text16.h"
#include "core/rect.h"

// Pure letterbox/scale geometry; the Win32 window adapters live in src/platform/display_window.h.
namespace obl::ui {

struct DisplayPoint {
    int x, y;
};

struct DisplaySize {
    int cx, cy;
};

// Integer bounds keep the presenter, cursor and GDI fallback on the same pixel edges.
inline screen::Rect ImageRect(int width, int height, int outputWidth, int outputHeight)
{
    width = std::max(width, 1);
    height = std::max(height, 1);
    outputWidth = std::max(outputWidth, 1);
    outputHeight = std::max(outputHeight, 1);
    double scale = std::min(static_cast<double>(outputWidth) / width, static_cast<double>(outputHeight) / height);
    int w = std::max(1, static_cast<int>(std::lround(width * scale)));
    int h = std::max(1, static_cast<int>(std::lround(height * scale)));
    int x = (outputWidth - w) / 2, y = (outputHeight - h) / 2;
    return {x, y, x + w, y + h};
}

// [Display] Scale: a finite number in [1, 4], the whole text consumed (anything else falls back to 1).
inline bool ParseScale(const Char16* text, double* scale)
{
    Char16* end;
    double value = text16::ParseDouble(text, &end);
    if (end == text || *end != 0 || !std::isfinite(value) || value < 1.0 || value > 4.0) return false;
    *scale = value;
    return true;
}

// Client size of the framed window for a game buffer at Scale, clamped to the monitor.
inline DisplaySize ScaledClientSize(int width, int height, double scale, int monitorWidth, int monitorHeight)
{
    return {std::min(monitorWidth, static_cast<int>(std::lround(width * scale))),
            std::min(monitorHeight, static_cast<int>(std::lround(height * scale)))};
}

// A client that fills the monitor in both directions becomes a borderless window on it.
inline bool CoversMonitor(DisplaySize client, int monitorWidth, int monitorHeight)
{
    return client.cx >= monitorWidth && client.cy >= monitorHeight;
}

inline DisplayPoint MapClientToGame(DisplayPoint client, DisplaySize game, const screen::Rect& image)
{
    // Sample at the centre of the client pixel: with the left edge, scales between 1 and 2 (e.g. 1.25)
    // map a game pixel's own client pixel back to its left neighbour. Integer scales are unaffected.
    // Preserve out-of-image coordinates so clicks on the bars never become clicks on an edge control.
    return {static_cast<int>(std::floor((client.x - image.left + 0.5) * game.cx / (image.right - image.left))),
            static_cast<int>(std::floor((client.y - image.top + 0.5) * game.cy / (image.bottom - image.top)))};
}

// A pointer on the client's outermost column or row is pressed against the screen edge when the window covers the monitor,
// and the map scrolls only when the cursor reaches the game's edge pixel. With bars between the two it would never get
// there, so that one client column or row moves onto the image's edge. Every other bar pixel stays outside the image.
inline DisplayPoint SnapClientEdgeToImage(DisplayPoint client, DisplaySize clientSize, const screen::Rect& image)
{
    if (client.x == 0 && client.x < image.left) client.x = image.left;
    if (client.x == clientSize.cx - 1 && client.x >= image.right) client.x = image.right - 1;
    if (client.y == 0 && client.y < image.top) client.y = image.top;
    if (client.y == clientSize.cy - 1 && client.y >= image.bottom) client.y = image.bottom - 1;
    return client;
}

inline DisplayPoint MapGameToClient(DisplayPoint point, DisplaySize game, const screen::Rect& image)
{
    // Aim at the centre of the source pixel so enlargement also round-trips at fractional scales.
    return {image.left + static_cast<int>(std::floor((point.x + 0.5) * (image.right - image.left) / game.cx)),
            image.top + static_cast<int>(std::floor((point.y + 0.5) * (image.bottom - image.top) / game.cy))};
}

} // namespace obl::ui
