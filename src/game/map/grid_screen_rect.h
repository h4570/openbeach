#pragma once

#include "game/map/grid_state.h"

#include <cstdint>

// The tile rectangle that a screen rectangle covers, used to redraw the path tiles under it. The map view goes through an
// Env:
//   void ScreenToWorld(int32_t x, int32_t y, float world[3])
//   void WorldToTile(const float world[3], uint32_t tile[2])
//   void TileToOrigin(uint32_t x, uint32_t y, float world[3])
//   void WorldToScreen(const float world[3], int32_t screen[2])
//   int32_t CameraX(), CameraY()
namespace obl::grid_screen_rect {

using map::Grid;

inline int32_t ClampTile(int32_t value)
{
    return value < 0 ? 0 : value > 0x80 ? 0x80 : value;
}

// The corners of the screen rectangle (left, top, right, bottom) go to world points and tiles; the tile centres (their origins
// plus half a tile in y) are projected back to find whether the rectangle starts or ends inside the edge tiles, and the tile
// range is widened by one on the side that is. Both start tests compare with the projection of the last tile. An inverted
// range becomes empty past the edge (0x80); everything is clamped to 0..128, and the size counts both ends.
template<class Env>
void ToTileRect(
        const Grid& grid, const int32_t screenRect[4], int32_t* outX, int32_t* outY, int32_t* outWidth, int32_t* outHeight, Env& env)
{
    float corner[3], other[3];
    env.ScreenToWorld(screenRect[0], screenRect[1], corner);
    env.ScreenToWorld(screenRect[2], screenRect[3], other);
    uint32_t first[2], last[2];
    env.WorldToTile(corner, first);
    env.WorldToTile(other, last);

    const float half = static_cast<float>(static_cast<uint32_t>(grid.m_tileSize) >> 1);
    float origin[3];
    env.TileToOrigin(first[0], first[1], origin);
    const float startPoint[3] = {origin[0], half + origin[1], origin[2]};
    env.TileToOrigin(last[0], last[1], origin);
    const float endPoint[3] = {origin[0], half + origin[1], origin[2]};
    int32_t screen[2];
    env.WorldToScreen(startPoint, screen);
    env.WorldToScreen(endPoint, screen);
    const int32_t endY = static_cast<int32_t>(static_cast<uint32_t>(screen[1]) + static_cast<uint32_t>(env.CameraY()));
    const int32_t endX = static_cast<int32_t>(static_cast<uint32_t>(screen[0]) + static_cast<uint32_t>(env.CameraX()));

    int32_t x0 = static_cast<int32_t>(first[0]), y0 = static_cast<int32_t>(first[1]);
    int32_t x1 = static_cast<int32_t>(last[0]), y1 = static_cast<int32_t>(last[1]);
    if (screenRect[0] < endX) --x0;
    if (screenRect[1] < endY && (x0 & 1)) --y0;
    if (endX < screenRect[2]) ++x1;
    if (endY < screenRect[3]) ++y1;
    if (x1 < x0) x1 = 0x80;
    if (y1 < y0) y1 = 0x80;
    x0 = ClampTile(x0);
    y0 = ClampTile(y0);
    x1 = ClampTile(x1);
    y1 = ClampTile(y1);
    *outX = x0;
    *outY = y0;
    *outWidth = x1 - x0 + 1;
    *outHeight = y1 - y0 + 1;
}

} // namespace obl::grid_screen_rect
