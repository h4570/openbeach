#include "game/ui/map_view.h"
#include "game/objects/object_base.h"
#include "game/ui/sprite_instance_geometry.h"

#include "game/map/grid_state.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite_anim.h"
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

// Whether `object` lies within half a view of the view: scrolling may bring it in next (sprite prefetch, sprite_anim.h).
bool IsNearView(MapView* view, const object::Base* object)
{
    renderer_ops::VisibleView around = ViewOf(*view->m_renderer);
    const int marginX = around.width / 2, marginY = around.height / 2;
    around.x -= marginX;
    around.y -= marginY;
    around.width += 2 * marginX;
    around.height += 2 * marginY;
    const RectQuery query = QueryOf(view, object);
    int stray = query.x;
    return renderer_ops::IsRectVisible(around, query.x, query.y, query.srcX, query.srcY, query.width, query.height,
            static_cast<int>(reinterpret_cast<uintptr_t>(&stray)));
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

// Starts decoding the animations of `object` in the background while it is near the view, where the host asked for it (the
// handheld build, sprite_anim.h): scrolling then finds them ready. A building draws several planes at once (image, depth, shadow,
// pick ...), so all of them; a guest has hundreds of animations, so the one it plays.
void PrefetchNearView(MapView* view, const object::Base* object)
{
    const auto* instance = static_cast<const SpriteInstance*>(object->m_sprite);
    if (!SpritePrefetchEnabled() || !instance || !instance->m_sprite || instance->m_animIndex < 0 ||
            instance->m_animIndex >= instance->m_sprite->m_animCount)
        return;
    if (!IsNearView(view, object)) return;
    const auto* sprite = instance->m_sprite;
    if (sprite->m_animCount <= 8)
        for (int i = 0; i < sprite->m_animCount; ++i) PrefetchSpriteAnim(sprite->m_anims[i]);
    else
        PrefetchSpriteAnim(sprite->m_anims[instance->m_animIndex]);
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
        const bool culls = map_culling::DrawObjectsCulls(type);
        if (culls && !CullQuery(this, object)) {
            PrefetchNearView(this, object);
            continue;
        }
        // Buildings and scenery are always drawn and clip themselves: their planes load when they first come into view.
        if (!culls) PrefetchNearView(this, object);
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
