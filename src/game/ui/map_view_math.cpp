#include "game/ui/map_view_math.h"

#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "game/ui/map_view_access.h"
#include "core/call_counter.h"

#include <cstdint>

namespace obl::ui {

TerrainView::TerrainView(const ui::MapView* view)
{
    terrain.mapWidth = view->m_mapWidth;
    terrain.mapHeight = view->m_mapHeight;
    if (const map::Grid* grid = game::GetGrid()) {
        terrain.heightScale = grid->m_heightScale;
        if (const map_pack::HeightSamples* height = grid->HeightMap()) {
            map = {height->width, height->height, height->samples.data()};
            terrain.heightMap = &map;
        }
    }
}

namespace {

using ui::MapView;

test::CallCounter g_callsTerrainHeight("ui.map-view.get-terrain-height");

test::CallCounter g_callsDepth("ui.map-view.get-depth");

test::CallCounter g_callsScreenToWorld("ui.map-view.screen-to-world-point");

} // namespace

double MapView::GetTerrainHeight(int x, int y) const
{
    g_callsTerrainHeight.Count();
    return map_math::TerrainHeight(TerrainView(this).terrain, x, y);
}

double MapView::GetTerrainHeightF(int worldX, int worldY) const
{
    return map_math::TerrainHeightAtWorld(TerrainView(this).terrain, worldX, worldY);
}

uint32_t MapView::GetDepth(const float* world) const
{
    g_callsDepth.Count();
    return map_math::Depth(TerrainView(this).terrain, m_depthPlaneOffset, m_depthSlantOffset, world);
}

int MapView::WorldToInt(const float* world, int* outX, int* outY)
{
    const map_math::MapPoint point = map_math::WorldToInt(world);
    *outX = point.x;
    *outY = point.y;
    return point.y;
}

grid_math::TileXY MapView::WorldToTile(const float* world)
{
    return game::GetGrid()->WorldPointToTile(world[0], world[1]);
}

int* MapView::TileToScreen(int* out, const uint32_t* tile) const
{
    const map_math::ScreenPixel pixel = map_math::TileToScreen(TerrainView(this).terrain, game::GetGrid()->m_tileSize, tile[0], tile[1]);
    out[0] = pixel.x;
    out[1] = pixel.y;
    return out;
}

float* MapView::ScreenToWorldPoint(float* out, const int* screen) const
{
    g_callsScreenToWorld.Count();
    map_math::ScreenToWorldPoint(TerrainView(this).terrain, screen[0], screen[1], out);
    return out;
}

} // namespace obl::ui
