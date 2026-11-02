#include "game/ui/map_view_access.h"

#include "game/ui/map_view_math.h"
#include "game/ui/render_geometry.h"
#include "core/call_counter.h"

namespace obl::ui {
namespace {

test::CallCounter g_calls("ui.map-view.world-to-screen");

}

void MapView::WorldToScreen(const float* world, int out[2]) const
{
    g_calls.Count();
    const int mapX = render_geometry::WorldToMapX(world[0]);
    const int mapY = render_geometry::WorldToMapY(world[1]);
    const render_geometry::ScreenPoint p =
            render_geometry::MapToScreen(mapX, mapY, render_geometry::FloatToIntWrap(GetTerrainHeight(mapX, mapY)), m_cameraX, m_cameraY);
    out[0] = p.x;
    out[1] = p.y;
}

} // namespace obl::ui
