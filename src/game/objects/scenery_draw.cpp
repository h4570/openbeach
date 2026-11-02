#include "game/ui/renderer_api.h"
#include "game/objects/guest.h"
#include "game/ui/sprite_anim.h"
#include "game/ui/blit.h"
#include "game/ui/sprite.h"
#include "game/game/settings_store.h"
#include "game/ui/sprite_instance_preview.h"
#include "game/ui/sprite_instance_geometry.h"
#include "game/objects/scenery_draw.h"
#include "game/objects/object_registry_api.h"
#include "game/game/game_accessors.h"
#include "game/objects/object_scenery.h"
#include "game/ui/map_view.h"
#include "core/call_counter.h"
#include "game/ui/sprite_instance_bake.h"
#include "game/ui/map_view_access.h"

namespace obl::object {
using game::GetObjectManager;
using game::GetRenderer;
using game::GetWorld;
using ui::GetMapView;

namespace {
using namespace world_data;
using object::Scenery;
using ui::SpriteInstance;
test::CallCounter g_counter("objects.scenery-draw");

void Direction(SpriteInstance& s, int dir)
{
    const int count = s.m_sprite->m_anims[s.m_animIndex]->m_directions;
    s.m_direction = dir >= count ? count - 1 : dir;
}
object::Base* Object(uint32_t id)
{
    return object_manager::ObjectById(GetObjectManager(), id);
}
void DrawGuest(object::Base& base, float* position)
{
    // Illness is evaluated before tone, and both results are rounded to float.
    auto& guest = *reinterpret_cast<object::Guest*>(&base);
    const float illness = GuestFoodIllness(guest);
    const float tone = static_cast<float>(GuestPaletteTone(guest));
    (static_cast<SpriteInstance*>(base.m_sprite))->DrawInWorldToned(position, GetWorld()->m_gameTime, tone, illness);
}
void Draw(Scenery& s, int kind)
{
    auto* sprite = static_cast<SpriteInstance*>(s.m_sprite);
    auto* world = GetWorld();
    if (kind == 0) {
        s.m_draw(s);
        if (s.m_lightSprite) {
            auto* renderer = GetRenderer();
            if ((s.m_lightsOn <= world->m_gameDayTime && world->m_previousDayTime < s.m_lightsOn) ||
                    (world->m_gameDayTime <= s.m_lightsOff && s.m_lightsOff < world->m_previousDayTime)) {
                renderer->m_lightsDirty = 1;
                renderer->m_lightsDirtyFrom = 0;
            }
            if (renderer->m_lightsEnabled) {
                Direction(*s.m_lightSprite, sprite->m_direction);
                s.m_drawLight(s);
            }
        }
    } else if (kind == 1) {
        (sprite)->DrawBuildingPreview(s.m_WorldXYZ, s.m_definition->m_GridWidth, s.m_definition->m_GridHeight, 0);
    } else if (kind == 2) {
        (sprite)->DrawInWorld(s.m_WorldXYZ, world->m_gameTime);
        if (s.m_state == 4) {
            if (s.m_AttachedGuest[0] != -1 && !Object(s.m_AttachedGuest[0])->m_visible) {
                s.m_canSelect = 1;
                auto* guest = Object(s.m_AttachedGuest[0]);
                if (guest->m_state == 29) {
                    Direction(*static_cast<SpriteInstance*>(guest->m_sprite), sprite->m_direction);
                    DrawGuest(*guest, s.m_WorldXYZ);
                }
            }
        } else
            s.m_canSelect = 0;
    } else if (kind == 3) {
        (sprite)->DrawInWorld(s.m_WorldXYZ, world->m_gameTime);
        static constexpr float offsets[2][8][2] = {{{-13, 20}, {-22, 8}, {-19, -20}, {-3, -24}, {15, -12}, {27, 0}, {22, -16}, {24, 2}},
                {{15, 20}, {-1, 28}, {-19, 14}, {-24, -4}, {-15, -12}, {2, -24}, {22, 16}, {6, 24}}};
        for (uint32_t i = 0; i < static_cast<uint32_t>(s.m_MaxNumAttachedGuests); ++i)
            if (s.m_AttachedGuest[i] != -1 && !Object(s.m_AttachedGuest[i])->m_visible) {
                float position[3] = {
                        s.m_WorldXYZ[0] + offsets[i][sprite->m_direction][0], s.m_WorldXYZ[1] + offsets[i][sprite->m_direction][1], 0};
                auto* guest = Object(s.m_AttachedGuest[i]);
                Direction(*static_cast<SpriteInstance*>(guest->m_sprite), (sprite->m_direction + 4) % 8);
                DrawGuest(*guest, position);
            }
    } else if (kind == 4) {
        (sprite)->BakeMasked(s.m_WorldXYZ);
        s.m_bakePending = 0;
        s.m_ScrXYRect = (sprite)->GetScreenAnchorRect(s.m_WorldXYZ);
        (sprite)->DrawInWorld(s.m_WorldXYZ, world->m_gameTime);
        s.m_draw = &SceneryDrawNothing;
    } else {
        if (!game::settings_store::Current().m_lightEffects ||
                !(s.m_lightsOn <= world->m_gameDayTime || world->m_gameDayTime <= s.m_lightsOff) || (s.m_vandalised && !s.m_flashLight))
            return;
        auto* light = s.m_lightSprite;
        const int offset = (light)->GetDrawOffsetX();
        const int hotY = light->m_sprite->m_anims[light->m_animIndex]->m_hotspotY;
        int xy[2];
        GetMapView()->ProjectWorldPoint(xy, s.m_WorldXYZ);
        xy[0] += offset;
        xy[1] += hotY;
        s.m_flashLight = 0;
        auto* anim = light->m_sprite->m_anims[0];
        int l = 0, t = 0, w = anim->m_width, h = anim->m_height;
        auto* renderer = GetRenderer();
        if (!obl::ui::renderer::ClipRect(renderer, &xy[0], &xy[1], &l, &t, &w, &h)) return;
        auto* frame = (anim)->GetFrame(0, sprite->m_direction);
        const int zoom = renderer->m_zoomLevel;
        if (zoom == 0 || zoom == 1 || zoom == 2)
            (zoom == 2 ? ui::pixel_primitives::RleToBytes8ZoomOut : ui::pixel_primitives::RleToBytes8)(
                    renderer->m_lightBuffer, frame, 0u, renderer->m_width, xy[0], xy[1], anim->m_width, anim->m_height, l, t, w, h);
    }
}
template<int Kind> void Run(Scenery& s)
{
    g_counter.Count();
    Draw(s, Kind);
}
} // namespace
void SceneryDrawPreview(Scenery& scenery)
{
    Run<1>(scenery);
}
void SceneryDrawLounger(Scenery& scenery)
{
    Run<2>(scenery);
}
void SceneryDrawBench(Scenery& scenery)
{
    Run<3>(scenery);
}
void SceneryDrawBurned(Scenery& scenery)
{
    Run<4>(scenery);
}
void SceneryDrawLight(Scenery& scenery)
{
    Run<5>(scenery);
}
void SceneryDrawGeneric(Scenery& scenery)
{
    scenery.object::Base::Draw();
}
void SceneryDrawNothing(Scenery&) {}
} // namespace obl::object

namespace obl::object {
void Scenery::Draw()
{
    object::Run<0>(*this);
}
} // namespace obl::object
