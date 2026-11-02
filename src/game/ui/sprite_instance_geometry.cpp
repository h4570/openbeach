#include "game/map/grid_state.h"
#include "game/ui/sprite_instance_geometry.h"
#include "game/ui/sprite.h"

#include "game/game/game_accessors.h"
#include "game/ui/map_view_access.h"
#include "render_geometry.h"
#include "core/call_counter.h"

#include <cstdint>
#include <cstring>
#include "game/ui/map_view_math.h"

namespace obl::ui {
namespace {

using Instance = ui::SpriteInstance;
using Anim = ui::SpriteAnim;
using render_geometry::AnimMetrics;

test::CallCounter g_callsOffset("ui.sprite-instance-get-draw-offset-x");

Anim* CurrentAnim(const Instance* instance)
{
    return instance->m_sprite->m_anims[instance->m_animIndex];
}

AnimMetrics MetricsOf(const Anim* anim)
{
    return {anim->m_hotspotX, anim->m_hotspotY, anim->m_width, anim->m_height, anim->m_name ? anim->m_name[0] : '\0'};
}

int DrawOffsetX(const Instance* instance)
{
    const Anim* anim = CurrentAnim(instance);
    const AnimMetrics current = MetricsOf(anim);
    const int type = instance->m_sprite->m_type;
    AnimMetrics first{};
    const bool needsFirst = render_geometry::UsesFirstAnim(type, instance->m_direction, current.nameFirst);
    if (needsFirst) first = MetricsOf(instance->m_sprite->m_anims[0]);
    int offset = 0;
    if (!render_geometry::DrawOffsetX(type, instance->m_direction, current, needsFirst ? &first : nullptr, &offset))
        return static_cast<int>(reinterpret_cast<intptr_t>(instance));
    return offset;
}

void AnchorRect(const Instance* instance, int rect[4], const float* world)
{
    ui::MapView* view = GetMapView();
    int screen[2];
    view->WorldToScreen(world, screen);
    const render_geometry::Rect r = render_geometry::AnchorRect(
            {screen[0], screen[1]}, view->m_cameraX, view->m_cameraY, DrawOffsetX(instance), MetricsOf(CurrentAnim(instance)));
    rect[0] = r.left;
    rect[1] = r.top;
    rect[2] = r.right;
    rect[3] = r.bottom;
}

} // namespace

uint16_t ActorTileDepthOffset(ui::MapView* view, const float* world)
{
    uint32_t tile[4]; // WorldToTile writes two words; spare room costs nothing
    ui::MapView::WorldToTile(tile, world);
    return game::GetGrid()->Cell(tile[0], tile[1]).m_zOffset;
}

namespace {

struct WorldPos {
    int x, y;
    int16_t depth;
};

WorldPos ComputeWorldPos(const Instance* instance, const float* world)
{
    ui::MapView* view = GetMapView();
    int screen[2];
    view->WorldToScreen(world, screen);
    WorldPos pos;
    pos.x = screen[0] + DrawOffsetX(instance);
    pos.y = screen[1] + CurrentAnim(instance)->m_hotspotY;
    const uint32_t mapDepth = view->GetDepth(world);
    const uint16_t tileDepth = instance->m_sprite->m_type == render_geometry::kTypeActor ? ActorTileDepthOffset(view, world) : 0;
    pos.depth = static_cast<int16_t>(render_geometry::DrawDepth(mapDepth, tileDepth));
    return pos;
}

} // namespace

int SpriteInstance::GetDrawOffsetX() const
{
    g_callsOffset.Count();
    return DrawOffsetX(this);
}

screen::Rect SpriteInstance::GetScreenAnchorRect(const float* world) const
{
    int rect[4];
    AnchorRect(this, rect, world);
    return {rect[0], rect[1], rect[2], rect[3]};
}

void SpriteInstance::GetWorldDrawPos(const float* world, float time, int* x, int* y, int16_t* depth)
{
    const WorldPos pos = ComputeWorldPos(this, world);
    this->UpdateAnim(time, world);
    *x = pos.x;
    *y = pos.y;
    *depth = pos.depth;
}

} // namespace obl::ui
