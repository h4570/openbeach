#include "game/ui/map_view.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite_instance_geometry.h"

#include "game/map/grid_state.h"
#include "game/ui/sprite.h"
#include "core/log.h"
#include "game/ui/map_culling.h"
#include "game/ui/map_view_access.h"
#include "game/ui/map_view_math.h"
#include "game/ui/render_geometry.h"
#include "game/ui/renderer_ops.h"

#include <cstdint>

namespace obl::ui {
namespace {

using map_culling::RectQuery;
using ui::MapView;
using ui::Renderer;
using ui::SpriteAnim;
using ui::SpriteInstance;

renderer_ops::VisibleView ViewOf(const Renderer& r)
{
    renderer_ops::VisibleView view;
    view.x = r.m_view[0];
    view.y = r.m_view[1];
    view.width = r.m_view[2];
    view.height = r.m_view[3];
    view.zoomLevel = r.m_zoomLevel;
    view.directMode = r.m_directMode != 0;
    view.overlay = r.m_renderVisibleObjects != 0;
    return view;
}

bool IsRectVisible(Renderer* renderer, const RectQuery& query)
{
    int x = query.x, y = query.y, srcX = query.srcX, srcY = query.srcY;
    uint32_t width = query.width, height = query.height;

    return renderer_ops::IsRectVisible(
            ViewOf(*renderer), x, y, srcX, srcY, width, height, static_cast<int>(reinterpret_cast<uintptr_t>(&x)));
}

double TerrainHeight(MapView* view, int mapX, int mapY)
{
    return view->GetTerrainHeight(mapX, mapY);
}

RectQuery QueryOf(MapView* view, const object::Base* object)
{
    const int mapX = render_geometry::WorldToMapX(object->m_WorldXYZ[0]);
    const int mapY = render_geometry::WorldToMapY(object->m_WorldXYZ[1]);
    const int terrainHeight = render_geometry::FloatToIntWrap(TerrainHeight(view, mapX, mapY));
    auto* instance = static_cast<SpriteInstance*>(object->m_sprite);
    const int offsetX = (instance)->GetDrawOffsetX();
    const SpriteAnim* anim = instance->m_sprite->m_anims[instance->m_animIndex];
    return map_culling::ObjectQuery(
            mapX, mapY, terrainHeight, view->m_cameraX, view->m_cameraY, offsetX, anim->m_hotspotY, anim->m_width, anim->m_height);
}

bool CullQuery(MapView* view, const object::Base* object)
{
    return IsRectVisible(view->m_renderer, QueryOf(view, object));
}

double VisibleTileRange(MapView* view)
{
    const Renderer* renderer = game::GetRenderer();
    map::Grid* grid = game::GetGrid();
    const int width = map_culling::ScaledViewSize(static_cast<uint32_t>(view->m_viewWidth), renderer->m_zoomScale);
    const int height = map_culling::ScaledViewSize(static_cast<uint32_t>(view->m_viewHeight), renderer->m_zoomScale);
    auto worldToTile = [grid](const map_culling::WorldPoint& point, uint32_t out[2]) {
        const grid_math::TileXY tile = grid->WorldPointToTile(point.x, point.y);
        out[0] = tile.x;
        out[1] = tile.y;
    };
    uint32_t topLeft[2], bottomRight[2];
    worldToTile(map_culling::ViewTopLeft(view->m_cameraX, view->m_cameraY), topLeft);
    worldToTile(map_culling::ViewBottomRight(view->m_cameraX, view->m_cameraY, width, height), bottomRight);
    const map_culling::TileRange range = map_culling::FixTileRange(topLeft, bottomRight);
    const int sea = map_culling::CountSeaTiles(grid->m_cells.data(), grid->m_width, grid->m_height, range.x0, range.y0,
            static_cast<int>(range.x1), static_cast<int>(range.y1));
    return map_culling::SeaShare(sea, range);
}

} // namespace

bool MapView::ClipObjectRect(const object::Base* object)
{
    return CullQuery(this, object);
}

void MapView::DrawObjects(object::Base** list, int count, int)
{
    Renderer* renderer = game::GetRenderer();
    renderer->m_trackDirtyRects = 1;
    m_guestObjectsDrawn = 0;
    for (int i = 0; i < count; ++i) {
        object::Base* object = list[i];
        if (!object->m_visible) continue;
        const uint8_t type = static_cast<uint8_t>(object->m_type);
        if (map_culling::DrawObjectsCulls(type) && !CullQuery(this, object)) continue;
        if (map_culling::IsGuestType(type)) ++m_guestObjectsDrawn;
        object->Draw();
    }
    game::GetRenderer()->m_trackDirtyRects = 0;
}

void MapView::RenderVisibleObjects(object::Base** list, int count)
{
    for (int i = 0; i < count; ++i) {
        object::Base* object = list[i];
        if (CullQuery(this, object)) object->DrawOverlay();
    }
}

double MapView::GetVisibleTileRange()
{
    return VisibleTileRange(this);
}

} // namespace obl::ui
