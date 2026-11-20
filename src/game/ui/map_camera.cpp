#include "game/ui/map_camera.h"

#include "render_geometry.h"
#include "game/ui/sprite_animation.h"

#include <cmath>

namespace obl::map_camera {
namespace {

using render_geometry::FloatToIntWrap;

constexpr double kInputEpsilon = 1.1920928955078125e-07;
constexpr float kMaxSpeed = 60.0f;
constexpr int kMarginX = 64;
constexpr int kMarginY = 32;
constexpr float kStopSpeed = 0.05f;

uint32_t Wrap(int value)
{
    return static_cast<uint32_t>(value);
}

struct AxisLimit {
    int32_t low, high;
    double floatHigh;
};

struct Limits {
    AxisLimit x, y;
};

AxisLimit MakeAxisLimit(uint32_t mapSize, uint32_t viewSpan, int margin)
{
    const uint64_t usable = static_cast<uint64_t>(viewSpan) + static_cast<uint64_t>(margin) * 2;
    if (usable > mapSize) {
        // Viewport exceeds the map along this axis: center the map and keep the origin even to match Update().
        const int32_t centered = (static_cast<int32_t>(static_cast<int64_t>(mapSize) - static_cast<int64_t>(viewSpan)) / 2) & ~1;
        return {centered, centered, static_cast<double>(centered)};
    }
    const int32_t high = static_cast<int32_t>(mapSize - viewSpan - static_cast<uint32_t>(margin));
    return {margin, high, static_cast<double>(high)};
}

bool LimitsFor(const ui::MapView& view, int zoomLevel, Limits& out)
{
    const uint32_t mapW = Wrap(view.m_mapWidth), mapH = Wrap(view.m_mapHeight);
    const uint32_t viewW = Wrap(view.m_viewWidth), viewH = Wrap(view.m_viewHeight);
    uint32_t spanW = 0, spanH = 0;
    switch (zoomLevel) {
    case 0:
        spanW = viewW >> 1;
        spanH = viewH >> 1;
        break;
    case 1:
        spanW = viewW;
        spanH = viewH;
        break;
    case 2:
        spanW = viewW * 2;
        spanH = viewH * 2;
        break;
    default: return false;
    }
    out.x = MakeAxisLimit(mapW, spanW, kMarginX);
    out.y = MakeAxisLimit(mapH, spanH, kMarginY);
    return true;
}

void ClampInt(int32_t& value, int32_t low, int32_t high)
{
    if (value < low)
        value = low;
    else if (value > high)
        value = high;
}

bool BelowOrNan(double a, double b)
{
    return !(a >= b);
}

} // namespace

double SmoothStep(float t)
{
    const double square = static_cast<double>(t) * t;
    const double cube = square * t;
    return square * 3.0 - (cube + cube);
}

void Clamp(ui::MapView& view, int zoomLevel)
{
    Limits limits;
    if (!LimitsFor(view, zoomLevel, limits)) return;
    ClampInt(view.m_cameraX, limits.x.low, limits.x.high);
    ClampInt(view.m_cameraY, limits.y.low, limits.y.high);

    if (BelowOrNan(view.m_cameraPositionX, limits.x.low))
        view.m_cameraPositionX = static_cast<float>(limits.x.low);
    else if (limits.x.floatHigh < view.m_cameraPositionX)
        view.m_cameraPositionX = static_cast<float>(limits.x.floatHigh);

    if (BelowOrNan(view.m_cameraPositionY, limits.y.low))
        view.m_cameraPositionY = static_cast<float>(limits.y.low);
    else if (limits.y.floatHigh < view.m_cameraPositionY)
        view.m_cameraPositionY = static_cast<float>(limits.y.floatHigh);
}

void AddVelocity(ui::MapView& view, float x, float y)
{
    if (std::fabs(static_cast<double>(x)) >= kInputEpsilon) view.m_lastInputX = x;
    if (std::fabs(static_cast<double>(y)) >= kInputEpsilon) view.m_lastInputY = y;
    view.m_velocityX = static_cast<float>(static_cast<double>(x) + view.m_velocityX);
    view.m_velocityY = static_cast<float>(static_cast<double>(y) + view.m_velocityY);
    if (view.m_velocityX > kMaxSpeed) view.m_velocityX = kMaxSpeed;
    if (view.m_velocityY > kMaxSpeed) view.m_velocityY = kMaxSpeed;
    if (BelowOrNan(view.m_velocityX, -kMaxSpeed)) view.m_velocityX = -kMaxSpeed;
    if (BelowOrNan(view.m_velocityY, -kMaxSpeed)) view.m_velocityY = -kMaxSpeed;
}

void UpdateTween(ui::MapView& view, float gameTime)
{
    if (!view.m_movingMap) return;

    const double share = (static_cast<double>(gameTime) - view.m_moveMapStartTime) / view.m_moveMapDuration;
    if (share > 1.0) {
        view.m_movingMap = 0;
        view.m_mapLocked = 0;
        view.m_cameraPositionX = static_cast<float>(view.m_moveMapDestX);
        view.m_cameraPositionY = static_cast<float>(view.m_moveMapDestY);
    }
    const double step = SmoothStep(static_cast<float>(share));
    const int dx = static_cast<int>(Wrap(view.m_moveMapDestX) - Wrap(view.m_moveMapStartX));
    const int dy = static_cast<int>(Wrap(view.m_moveMapDestY) - Wrap(view.m_moveMapStartY));
    view.m_cameraPositionX = static_cast<float>(dx * step + view.m_moveMapStartX);
    view.m_cameraPositionY = static_cast<float>(dy * step + view.m_moveMapStartY);
}

void SetPosition(ui::MapView& view, int zoomLevel, int x, int y)
{
    view.m_cameraPositionX = static_cast<float>(x);
    view.m_cameraPositionY = static_cast<float>(y);
    Clamp(view, zoomLevel);
}

bool ScrollTo(ui::MapView& view, const Screen& screen, const map_math::Terrain& terrain, float x, float y, float seconds, float gameTime)
{
    const int mapX = FloatToIntWrap(x);
    const int mapY = FloatToIntWrap(static_cast<double>(y) * render_geometry::kWorldYScale);
    const int lift = FloatToIntWrap(map_math::TerrainHeight(terrain, mapX, mapY));

    const int left = static_cast<int>(Wrap(mapX) - (screen.width >> 1));
    const int top = static_cast<int>(Wrap(mapY) - Wrap(lift) - (screen.height >> 1));
    if (BelowOrNan(seconds, 1.1920928955078125e-07)) {
        view.m_cameraPositionX = static_cast<float>(left);
        view.m_cameraPositionY = static_cast<float>(top);
        Clamp(view, screen.zoomLevel);
        return true;
    }
    view.m_moveMapDuration = seconds;
    view.m_movingMap = 1;
    view.m_moveMapStartTime = gameTime;
    view.m_moveMapStartX = FloatToIntWrap(view.m_cameraPositionX);
    view.m_moveMapStartY = FloatToIntWrap(view.m_cameraPositionY);
    view.m_moveMapDestX = left;
    view.m_moveMapDestY = top;
    view.m_mapLocked = 1;
    view.m_velocityX = 0.0f;
    view.m_velocityY = 0.0f;
    return false;
}

Frame Update(ui::MapView& view, const Screen& screen, float gameTime)
{
    Frame frame{};
    if (view.m_mapLocked) {
        if (view.m_movingMap) {
            frame.redraw = true;
            UpdateTween(view, gameTime);
            view.m_cameraX = FloatToIntWrap(view.m_cameraPositionX);
            view.m_cameraY = FloatToIntWrap(view.m_cameraPositionY);
            Clamp(view, screen.zoomLevel);
        }
        return frame;
    }

    view.m_cameraPositionX = static_cast<float>(static_cast<double>(view.m_velocityX) + view.m_cameraPositionX);
    view.m_cameraPositionY = static_cast<float>(static_cast<double>(view.m_velocityY) + view.m_cameraPositionY);
    view.m_cameraX = FloatToIntWrap(view.m_cameraPositionX);
    view.m_cameraY = FloatToIntWrap(view.m_cameraPositionY);
    Clamp(view, screen.zoomLevel);

    if (view.m_cameraX & 1) view.m_cameraX = static_cast<int>(Wrap(view.m_cameraX) + 1);
    if (view.m_cameraY & 1) view.m_cameraY = static_cast<int>(Wrap(view.m_cameraY) + 1);

    frame.centre[0] = FloatToIntWrap(static_cast<double>(screen.width) / screen.zoomScale * 0.5 + view.m_cameraX);
    frame.centre[1] = FloatToIntWrap(static_cast<double>(screen.height) / screen.zoomScale * 0.5 + view.m_cameraY);
    frame.listener = true;

    if (BelowOrNan(std::fabs(static_cast<double>(view.m_lastInputX)), kInputEpsilon))
        view.m_velocityX = static_cast<float>(static_cast<double>(view.m_velocityX) * 0.5);
    if (BelowOrNan(std::fabs(static_cast<double>(view.m_lastInputY)), kInputEpsilon))
        view.m_velocityY = static_cast<float>(static_cast<double>(view.m_velocityY) * 0.5);

    const double vx = view.m_velocityX, vy = view.m_velocityY;
    const float squared = static_cast<float>(vx * vx + vy * vy);

    if (squared == 0.0f && !std::signbit(squared)) {
        view.m_velocityX = view.m_velocityY = 0.0f;
    } else if (!(sprite_animation::FastSqrt(squared) > kStopSpeed)) {
        view.m_velocityX = view.m_velocityY = 0.0f;
    } else {
        frame.redraw = true;
    }
    return frame;
}

void EndFrame(ui::MapView& view)
{
    view.m_lastInputX = 0.0f;
    view.m_lastInputY = 0.0f;
}

void Stop(ui::MapView& view)
{
    view.m_velocityX = 0.0f;
    view.m_velocityY = 0.0f;
}

void Lock(ui::MapView& view)
{
    view.m_mapLocked = 1;
}
void Unlock(ui::MapView& view)
{
    view.m_mapLocked = 0;
}

void SetViewportSize(ui::MapView& view, const Screen& screen, int width, int height)
{
    view.m_viewLeft = 0;
    view.m_viewTop = 0;
    view.m_viewWidth = width;
    view.m_viewHeight = static_cast<int>(Wrap(height) - Wrap(screen.guiBarHeight));
}

void ResetViewport(ui::MapView& view, const Screen& screen)
{
    view.m_viewHeight = static_cast<int>(screen.height - Wrap(screen.guiBarHeight));
}

} // namespace obl::map_camera
