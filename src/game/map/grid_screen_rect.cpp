#include "game/map/grid_screen_rect.h"

#include "game/game/game_accessors.h"
#include "game/map/grid_state.h"
#include "game/ui/map_view.h"
#include "game/ui/map_view_access.h"
#include "game/ui/map_view_math.h"

#include <cstdint>

namespace obl::map {
namespace {

// The map view behind the rectangle conversion.
struct ViewEnv {
    const Grid& grid;

    void ScreenToWorld(int32_t x, int32_t y, float world[3])
    {
        int32_t point[2] = {x, y};
        ui::GetMapView()->ScreenToWorldPoint(world, point);
    }
    void WorldToTile(const float world[3], uint32_t tile[2])
    {
        const grid_math::TileXY found = grid.WorldPointToTile(world[0], world[1]);
        tile[0] = found.x;
        tile[1] = found.y;
    }
    void TileToOrigin(uint32_t x, uint32_t y, float world[3])
    {
        const grid_math::WorldPoint origin = grid.TileToWorldOrigin(x, y);
        world[0] = origin.x;
        world[1] = origin.y;
        world[2] = origin.z;
    }
    void WorldToScreen(const float world[3], int32_t screen[2])
    {
        float copy[3] = {world[0], world[1], world[2]};
        ui::GetMapView()->ProjectWorldPoint(screen, copy);
    }
    int32_t CameraX() { return ui::GetMapView()->m_cameraX; }
    int32_t CameraY() { return ui::GetMapView()->m_cameraY; }
};

} // namespace

void Grid::ScreenRectToTileRect(const int32_t* rect, int32_t* outX, int32_t* outY, int32_t* outWidth, int32_t* outHeight) const
{
    ViewEnv env{*this};
    grid_screen_rect::ToTileRect(*this, rect, outX, outY, outWidth, outHeight, env);
}

} // namespace obl::map
