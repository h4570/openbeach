// Screen rectangles to tile rectangles on the terrain grid (src/game/map/grid_screen_rect.h).

#include "unit_test.h"

#include "game/map/grid_screen_rect.h"
#include "game/map/grid_state.h"

using namespace obl;
using obl::map::Grid;

namespace {
// A flat map: a screen point is the world point (x, y), a tile is 64 units, the tile origin is its corner and a world point projects to itself.
struct FlatEnv {
    int32_t cameraX = 0, cameraY = 0;
    void ScreenToWorld(int32_t x, int32_t y, float world[3])
    {
        world[0] = static_cast<float>(x);
        world[1] = static_cast<float>(y);
        world[2] = 0;
    }
    void WorldToTile(const float world[3], uint32_t tile[2])
    {
        tile[0] = static_cast<uint32_t>(static_cast<int32_t>(world[0]) / 64);
        tile[1] = static_cast<uint32_t>(static_cast<int32_t>(world[1]) / 64);
    }
    void TileToOrigin(uint32_t x, uint32_t y, float world[3])
    {
        world[0] = static_cast<float>(x * 64);
        world[1] = static_cast<float>(y * 64);
        world[2] = 0;
    }
    void WorldToScreen(const float world[3], int32_t screen[2])
    {
        screen[0] = unit::TruncToInt32(world[0]);
        screen[1] = unit::TruncToInt32(world[1]);
    }
    int32_t CameraX() { return cameraX; }
    int32_t CameraY() { return cameraY; }
};
} // namespace

OBL_TEST_CASE("grid_screen_rect.screen_rect_widens_by_the_tiles_it_starts_and_ends_inside_and_clamps")
{
    Grid grid{};
    grid.m_tileSize = 64;
    FlatEnv env;
    int32_t x, y, width, height;
    const int32_t inside[4] = {70, 100, 200, 300};
    grid_screen_rect::ToTileRect(grid, inside, &x, &y, &width, &height, env);
    // tiles (1, 1)..(3, 4). Both tests of the start compare with the projection of the LAST tile: 70 < 192 widens the start to x 0
    // (even, so y stays), and the end grows on both axes (192 < 200, 288 < 300).
    OBL_CHECK_EQ(x, 0);
    OBL_CHECK_EQ(y, 1);
    OBL_CHECK_EQ(width, 5);
    OBL_CHECK_EQ(height, 5);
    const int32_t inverted[4] = {500, 500, 100, 100};
    grid_screen_rect::ToTileRect(grid, inverted, &x, &y, &width, &height, env);
    OBL_CHECK_EQ(x + width - 1, 0x80);
    OBL_CHECK_EQ(y + height - 1, 0x80);
    const int32_t outside[4] = {-9000, -9000, 99999, 99999};
    grid_screen_rect::ToTileRect(grid, outside, &x, &y, &width, &height, env);
    OBL_CHECK_EQ(x, 0);
    OBL_CHECK_EQ(y, 0);
    OBL_CHECK_EQ(width, 0x81);
    OBL_CHECK_EQ(height, 0x81);
}
