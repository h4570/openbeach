#include "game/ui/renderer_api.h"
#include "game/ui/map_view.h"

#include "game/map/grid_state.h"
#include "game/map/grid_cell.h"
#include "core/log.h"
#include "game/ui/map_effects.h"
#include "game/ui/map_view_access.h"
#include "game/ui/map_view_math.h"
#include "game/ui/render_geometry.h"
#include "game/map/water_anim.h"
#include "game/ui/renderer_types.h"
#include "core/rect.h"
#include "core/call_counter.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace obl::ui {
namespace {

using ui::MapView;
using ui::Renderer;

test::CallCounter g_calls("ui.map-effects");
test::CallCounter g_callsDarken("ui.map-view.darken-by-depth");
test::CallCounter g_callsSea("ui.map-view.draw-sea");

uint16_t& HalfMask()
{
    return game::GetBlitHalfMask();
}

uint8_t*& WaterSource()
{
    return WaterBlockImage().m_pixels;
}

map_effects::SceneBuffer SceneOf(const Renderer* renderer)
{
    return {renderer->m_workBuffer, renderer->m_width, renderer->m_height};
}

void DarkenScene(MapView* view)
{
    map_effects::DarkenByDepth(
            SceneOf(game::GetRenderer()), view->m_cameraY, view->m_depthPlaneOffset, view->m_depthSlantOffset, HalfMask());
}

map_effects::TileHighlight HighlightInput(MapView* view, uint32_t tileX, uint32_t tileY, uint32_t color)
{
    map::Grid* grid = game::GetGrid();
    map_effects::TileHighlight h{};
    h.tileSize = static_cast<uint32_t>(grid->m_tileSize);
    h.tile[0] = tileX;
    h.tile[1] = tileY;
    const grid_math::TileXY neighbour2 = grid->GetNeighbour(tileX, tileY, 2);
    h.neighbour2[0] = neighbour2.x;
    h.neighbour2[1] = neighbour2.y;
    const grid_math::TileXY neighbour3 = grid->GetNeighbour(tileX, tileY, 3);
    h.neighbour3[0] = neighbour3.x;
    h.neighbour3[1] = neighbour3.y;
    const grid_math::WorldPoint origin = grid->TileToWorldOrigin(tileX, tileY);
    h.origin[0] = origin.x;
    h.origin[1] = origin.y;
    h.origin[2] = origin.z;
    h.cameraX = view->m_cameraX;
    h.cameraY = view->m_cameraY;
    h.zoomLevel = view->m_renderer->m_zoomLevel;
    h.zoomScale = game::GetRenderer()->m_zoomScale;
    h.planeOffset = view->m_depthPlaneOffset;
    h.slant = view->m_depthSlantOffset;
    h.color = static_cast<uint16_t>(color);
    return h;
}

void HighlightTile(MapView* view, uint32_t tileX, uint32_t tileY, uint32_t color, const map_effects::SceneBuffer& scene,
        map_effects::HighlightStats* stats = nullptr)
{
    const TerrainView terrain(view);
    map_effects::DrawTileHighlight(scene, terrain.terrain, HighlightInput(view, tileX, tileY, color), stats);
}

void DrawSeaBlocks(MapView* view)
{
    Renderer* renderer = game::GetRenderer();
    const map_effects::SeaRange range = map_effects::VisibleSeaBlocks(
            view->m_cameraX, view->m_cameraY, view->m_viewWidth, view->m_viewHeight, view->m_blockSize, renderer->m_zoomScale);
    for (int row = range.firstRow; row < range.endRow; ++row) {
        for (int column = range.firstColumn; column < range.endColumn; ++column) {
            const ui::MapBlock& block = view->m_blocks[view->m_blocksPerRow * row + column];
            if (!block.m_loaded) continue;
            const map_effects::SeaBlockOrigin origin =
                    map_effects::BlockOrigin(view->m_blockSize, column, row, view->m_cameraX, view->m_cameraY);
            int pos[2] = {origin.x, origin.y};
            uint32_t rect[4] = {0, 0, 0, 0};
            const void* image = &WaterBlockImage();
            const int useMask = block.m_usesWaterMask != 0 ? 1 : 0;
            WaterSource() = reinterpret_cast<uint8_t*>(block.m_pixels);
            ui::renderer::DrawWaterBlock(view->m_renderer, column, row, pos, rect, const_cast<void*>(image), 8, 0, useMask);
        }
    }

    screen::Rect dirty{};
    while (ui::renderer::PopDirtyRect(renderer, &dirty)) {}
    WaterSource() = nullptr;
}

} // namespace

void MapView::DarkenByDepth()
{
    g_calls.Count();
    g_callsDarken.Count();
    DarkenScene(this);
}

bool MapView::DrawSea()
{
    g_calls.Count();
    g_callsSea.Count();
    DrawSeaBlocks(this);
    return true;
}

bool MapView::DrawTileHighlight(uint32_t tileX, uint32_t tileY, uint32_t color)
{
    g_calls.Count();

    HighlightTile(this, tileX, tileY, color, SceneOf(game::GetRenderer()));
    return true;
}

} // namespace obl::ui
