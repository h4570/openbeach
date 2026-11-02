#pragma once

#include "core/float_to_int.h"
#include "game/ui/gui_manager_types.h"

#include <cstdint>

namespace obl::gui_radar {

struct Rect {
    int32_t x0, y0, x1, y1;
};

struct ViewRect {
    int32_t x0, y0, x1, y1;
};

inline constexpr double kMapBorderX = 64.0;
inline constexpr double kMapBorderY = 32.0;
inline constexpr double kBarSmall = 70.0;
inline constexpr double kBarLarge = 88.0;
inline constexpr double kSmallScreenWidth = 800.0;

inline ViewRect CalcViewRect(const Rect& widget, uint32_t screenWidth, uint32_t screenHeight, int32_t mapWidth, int32_t mapHeight)
{
    const double width = static_cast<double>(screenWidth);
    const double height = static_cast<double>(screenHeight);
    const float widgetWidth = static_cast<float>(widget.x1 - widget.x0);
    const float mapX = static_cast<float>(static_cast<double>(mapWidth) - kMapBorderX * 2);
    const float mapY = static_cast<float>(static_cast<double>(mapHeight) - kMapBorderY * 2);
    const double bar = width < kSmallScreenWidth ? kBarSmall : kBarLarge;
    ViewRect view{widget.x0, widget.y0, 0, 0};
    view.x1 = FloatToIntWrap(width / mapX * widgetWidth + view.x0);
    view.y1 = FloatToIntWrap((height - bar) / mapY * static_cast<double>(widget.y1 - widget.y0) + view.y0);
    return view;
}

// GuiManager::UpdateRadarMouse: the cursor is over the panel when it is inside the stored panel rectangle, except that the bottom edge is the
// current panel sprite's height below the top (the sprite changes between the big and the small panel; the stored bottom is not used).
inline bool PanelHovered(const Rect& panel, int spriteHeight, int x, int y)
{
    return x <= panel.x1 && panel.x0 <= x && y <= spriteHeight + panel.y0 && panel.y0 <= y;
}

struct Point {
    int32_t x, y;
};

inline Point ClickCameraTarget(const Rect& widget, const ViewRect& view, int x, int y, int32_t mapWidth, int32_t mapHeight, int zoomLevel,
        uint32_t screenWidth, uint32_t screenHeight)
{
    const int32_t offsetX = (x - widget.x0) - (view.x1 - view.x0) / 2;
    const int32_t offsetY = (y - widget.y0) - (view.y1 - view.y0) / 2;
    Point target{FloatToIntWrap(static_cast<double>(static_cast<float>(offsetX)) / static_cast<double>(widget.x1 - widget.x0) * mapWidth),
            FloatToIntWrap(static_cast<double>(static_cast<float>(offsetY)) / static_cast<double>(widget.y1 - widget.y0) * mapHeight)};
    const uint32_t quarterX = screenWidth >> 2, quarterY = screenHeight >> 2;
    if (zoomLevel == 2) {
        target.x = static_cast<int32_t>(static_cast<uint32_t>(target.x) - quarterX);
        target.y = static_cast<int32_t>(static_cast<uint32_t>(target.y) - quarterY);
    } else if (zoomLevel == 0) {
        target.x = static_cast<int32_t>(static_cast<uint32_t>(target.x) + quarterX);
        target.y = static_cast<int32_t>(static_cast<uint32_t>(target.y) + quarterY);
    }
    return target;
}

inline ViewRect ShiftedViewRect(const Rect& widget, const ViewRect& view, float cameraX, float cameraY, int32_t mapWidth, int32_t mapHeight)
{
    const float shiftedX = static_cast<float>(static_cast<double>(cameraX) - kMapBorderX);
    const float shiftedY = static_cast<float>(static_cast<double>(cameraY) - kMapBorderY);
    const float widgetWidth = static_cast<float>(widget.x1 - widget.x0);
    const float mapX = static_cast<float>(static_cast<double>(mapWidth) - kMapBorderX * 2);
    const double mapY = static_cast<double>(mapHeight) - kMapBorderY * 2;
    const int32_t newY = FloatToIntWrap(static_cast<double>(shiftedY) / mapY * static_cast<double>(widget.y1 - widget.y0)) + widget.y0;
    const int32_t newX =
            widget.x0 + FloatToIntWrap(static_cast<double>(shiftedX) / static_cast<double>(mapX) * static_cast<double>(widgetWidth));
    const int32_t dx = newX - view.x0, dy = newY - view.y0;
    return {view.x0 + dx, view.y0 + dy, view.x1 + dx, view.y1 + dy};
}

struct Line {
    int32_t x0, y0, x1, y1;
};

inline int CameraRectLines(const ViewRect& view, int zoomLevel, Line (&out)[4])
{
    int32_t farX, farY;
    if (zoomLevel == 1) {
        farX = view.x1;
        farY = view.y1;
    } else if (zoomLevel == 2) {
        farX = view.x1 * 2 - view.x0;
        farY = view.y1 * 2 - view.y0;
    } else if (zoomLevel == 0) {
        farX = view.x1 - (view.x1 - view.x0) / 2;
        farY = view.y1 - (view.y1 - view.y0) / 2;
    } else {
        return 0;
    }
    out[0] = {view.x0, view.y0, farX, view.y0};
    out[1] = {view.x0, farY, farX, farY};
    out[2] = {view.x0, view.y0, view.x0, farY};
    out[3] = {farX, view.y0, farX, farY};
    return 4;
}

struct MarkerFrame {
    float widgetWidth, widgetHeight, widgetX, widgetY, mapWidth, mapHeight;
};
inline double MarkerU(const MarkerFrame& f, float worldX)
{
    return static_cast<double>(worldX) / f.mapWidth * f.widgetWidth + f.widgetX;
}
inline double MarkerV(const MarkerFrame& f, float worldY)
{
    return static_cast<double>(worldY) * 0.5 / f.mapHeight * f.widgetHeight + f.widgetY;
}
inline Point MarkerPixel(const MarkerFrame& f, float worldX, float worldY)
{
    return {FloatToIntWrap(MarkerU(f, worldX)), FloatToIntWrap(MarkerV(f, worldY))};
}
inline Rect MarkerSquare(const MarkerFrame& f, float worldX, float worldY)
{
    const double u = MarkerU(f, worldX), v = MarkerV(f, worldY);
    return {FloatToIntWrap(u - 1.0), FloatToIntWrap(v - 1.0), FloatToIntWrap(u + 1.0), FloatToIntWrap(v + 1.0)};
}
// A building footprint (grid width and height, swapped when the sprite is mirrored): a bar a quarter of the width to each side, rising from
// the anchor by the full depth at the top and half of it at the bottom edge. Rect order: left, top, right, bottom.
inline Rect MarkerFootprint(const MarkerFrame& f, float worldX, float worldY, uint32_t widthCells, uint32_t depthCells)
{
    const double u = MarkerU(f, worldX), v = MarkerV(f, worldY);
    const double across = static_cast<double>(widthCells) * 0.25, depth = static_cast<double>(depthCells);
    return {FloatToIntWrap(u - across), FloatToIntWrap(v - depth), FloatToIntWrap(across + u), FloatToIntWrap(v - depth * 0.5)};
}

inline int32_t ProgressBarY(uint32_t screenWidth, uint32_t screenHeight)
{
    return static_cast<int32_t>(screenHeight) - (screenWidth < 800 ? 0xaa : 0xbe);
}
// GuiManager::DrawProgressBar: where the panel sprite is drawn; the small strip (m_big = 0) lies 0x41 pixels lower.
inline Point ProgressSpritePos(int32_t x, int32_t y, bool big)
{
    return {x, big ? y : y + 0x41};
}

} // namespace obl::gui_radar

namespace obl::ui {
struct GuiManager;
} // namespace obl::ui
namespace obl::gui {
void UpdateRadarViewRectDirect(void* gui);
} // namespace obl::gui
