#pragma once

#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"

#include <cstdint>

// IsVisibleAgainstBuilding (called by the selection code of the GUI): is the moving object drawn in front of
// the building, i.e. not hidden behind it? An object that is not behind the building on the map (its world Y is not smaller) is visible without
// looking at the screen. Otherwise the pixel the object stands on is looked up in the work buffer (high word = depth of whatever was drawn there):
// the object is visible when its own depth (the map depth of its position, plus its tile's depth offset, plus 0x30) is not below that.
// Everything outside the object goes through an `Env` (the game binds it in src/game/objects/mover_visibility.cpp, the unit tests fake it):
//   const Entity* Building(int id)                       the object manager's lookup (null when no such object)
//   void WorldToScreen(const float* world, int out[2])       world-to-screen projection
//   uint32_t Depth(const float* world)                       the projected depth (the low 16 bits are used)
//   void TilePosition(const Mover&, uint32_t out[2])  GetTilePosition (entity_accessors.h)
//   int ZoomLevel                                          the renderer's zoom level (2 = zoomed out: the screen point is halved)
//   int ViewWidth / ViewHeight                           Renderer m_view[2] / m_view[3]
//   int BufferStride                                       Renderer +4 (words per row of the work buffer)
//   uint32_t BufferWord(int index)                           the work buffer
//   uint32_t GridWidth / GridHeight                      the global grid
//   uint16_t ZOffset(uint32_t cellIndex)                     grid cell z offset
namespace obl::moving {

inline constexpr uint16_t kVisibilityDepthBias = 0x30;

// The screen point, clamped: halved toward zero when zoomed out, negative coordinates to 0, then at most the last pixel of the view
// (the upper clamp is a signed compare, so a zero or negative view size pins the point to size - 1).
struct ScreenPoint {
    int x, y;
};
inline ScreenPoint ClampToView(int x, int y, int zoomLevel, int viewWidth, int viewHeight)
{
    if (zoomLevel == 2) {
        x /= 2;
        y /= 2;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= viewWidth) x = viewWidth - 1;
    if (y >= viewHeight) y = viewHeight - 1;
    return {x, y};
}

// The tile wraps around the grid (the unsigned remainders), the cell index is row * width + column.
inline uint32_t WrappedCellIndex(uint32_t tileX, uint32_t tileY, uint32_t gridWidth, uint32_t gridHeight)
{
    return (tileY % gridHeight) * gridWidth + tileX % gridWidth;
}

template<class Env> bool IsVisibleAgainstBuilding(const object::Mover& object, int buildingId, Env& env)
{
    const object::Base* building = env.Building(buildingId);
    if (!building) return false;
    // An unordered pair (NaN) counts as "smaller" and goes on, so only a real >= answers early.
    if (object.m_WorldXYZ[1] >= building->m_WorldXYZ[1]) return true;

    int screen[2];
    env.WorldToScreen(object.m_WorldXYZ, screen);
    const ScreenPoint point = ClampToView(screen[0], screen[1], env.ZoomLevel(), env.ViewWidth(), env.ViewHeight());
    const uint16_t depth = static_cast<uint16_t>(env.Depth(object.m_WorldXYZ));
    uint32_t tile[2];
    env.TilePosition(object, tile);
    const uint16_t objectDepth = static_cast<uint16_t>(
            env.ZOffset(WrappedCellIndex(tile[0], tile[1], env.GridWidth(), env.GridHeight())) + depth + kVisibilityDepthBias);
    const uint32_t word = env.BufferWord(env.BufferStride() * point.y + point.x);
    return objectDepth >= (word >> 16);
}

} // namespace obl::moving
