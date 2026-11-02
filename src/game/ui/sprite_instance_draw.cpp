#include "game/ui/renderer_api.h"
#include "game/ui/sprite.h"
#include "game/ui/palette_modifiers.h"

#include "game/game/game_accessors.h"
#include "core/log.h"
#include "game/ui/map_view_access.h"
#include "render_geometry.h"
#include "game/ui/sprite_instance_geometry.h"
#include "game/ui/sprite_instance_render.h"
#include "game/ui/sprite_observer.h"
#include "core/call_counter.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include "game/ui/map_view_math.h"

namespace obl::ui {
namespace {

using Instance = ui::SpriteInstance;
using Anim = ui::SpriteAnim;
// Call counters (`call-report ui.sprite-instance-draw`).
test::CallCounter g_calls("ui.sprite-instance-draw");

const Anim* CurrentAnim(const Instance* instance)
{
    return instance->m_sprite->m_anims[instance->m_animIndex];
}

struct Plan {
    bool draws = false;
    int x = 0, y = 0, left = 0, top = 0, right = 0, bottom = 0;
    uint32_t depth = 0;
};

// The clip rectangle starts as the whole frame; ui::renderer::ClipRect narrows it (and moves the destination).
bool Clip(const Instance* instance, Plan* plan)
{
    const Anim* anim = CurrentAnim(instance);
    plan->left = plan->top = 0;
    plan->right = anim->m_width;
    plan->bottom = anim->m_height;
    plan->draws =
            obl::ui::renderer::ClipRect(game::GetRenderer(), &plan->x, &plan->y, &plan->left, &plan->top, &plan->right, &plan->bottom) != 0;
    return plan->draws;
}

Plan PlanClipped(const Instance* instance, int x, int y, uint32_t depth)
{
    Plan plan;
    plan.x = x;
    plan.y = y;
    plan.depth = depth;
    Clip(instance, &plan);
    return plan;
}

// Pure: reads the instance, the camera and the grid. UpdateAnim does not change any of it, so the plan may be made
// before or after the animation advances.
Plan PlanInWorld(const Instance* instance, const float* world)
{
    ui::MapView* view = GetMapView();
    int screen[2];
    view->WorldToScreen(world, screen);
    Plan plan;
    plan.x = screen[0] + instance->GetDrawOffsetX();
    plan.y = screen[1] + CurrentAnim(instance)->m_hotspotY;
    if (!Clip(instance, &plan)) return plan;
    const uint32_t mapDepth = view->GetDepth(world);
    const uint16_t tileDepth = instance->m_sprite->m_type == render_geometry::kTypeActor ? ActorTileDepthOffset(view, world) : 0;
    plan.depth = render_geometry::InWorldDepth(mapDepth, tileDepth);
    return plan;
}

void DrawEntry(Instance* instance, const Plan& plan)
{
    instance->Draw(plan.x, plan.y, plan.left, plan.top, plan.right, plan.bottom, plan.depth);
}

void UpdateAnim(Instance* instance, float time, const float* world)
{
    instance->UpdateAnim(time, world);
}

void UpdateAnimNoSound(Instance* instance, float time)
{
    instance->UpdateAnimNoSound(time);
}

void* PaletteModifiers()
{
    return ui::CurrentPaletteModifiers();
}

const uint16_t* BuildToned(Instance* instance, float scale, float blend)
{
    return ui::BuildTonedPalette(PaletteModifiers(), instance, scale, blend);
}

const uint16_t* BuildTonedEx(Instance* instance, float scale, float blend, uint32_t extra)
{
    return ui::BuildTonedPaletteWithExtra(PaletteModifiers(), instance, scale, blend, extra);
}

// ---- the functions ----

void DrawClippedFrames(Instance* instance, int x, int y, uint32_t depth, float time)
{
    const Plan plan = PlanClipped(instance, x, y, depth);
    if (!plan.draws) return;
    UpdateAnimNoSound(instance, time);
    DrawEntry(instance, plan);
}

void DrawClippedTonedFrames(Instance* instance, int x, int y, uint32_t depth, float time, float scale, float blend)
{
    const Plan plan = PlanClipped(instance, x, y, depth);
    if (!plan.draws) return;
    UpdateAnimNoSound(instance, time);
    // Unconditional here (unlike DrawInWorldToned): the builder answers null for sprites without a palette modifier.
    instance->m_overridePalette = BuildTonedEx(instance, scale, blend, 0);
    DrawEntry(instance, plan);
}

void DrawInWorldFrames(Instance* instance, const float* world, float time)
{
    const Plan plan = PlanInWorld(instance, world);
    UpdateAnim(instance, time, world);
    if (plan.draws) DrawEntry(instance, plan);
}

void DrawInWorldTonedFrames(Instance* instance, const float* world, float time, float scale, float blend)
{
    if (render_geometry::NeedsTonedPalette(scale, blend)) instance->m_overridePalette = BuildToned(instance, scale, blend);
    DrawInWorldFrames(instance, world, time);
}

void DrawInWorldTonedExFrames(Instance* instance, const float* world, float time, float scale, float blend, uint32_t extra)
{
    if (render_geometry::NeedsTonedPalette(scale, blend)) instance->m_overridePalette = BuildTonedEx(instance, scale, blend, extra);
    DrawInWorldFrames(instance, world, time);
}

} // namespace

void SpriteInstance::DrawClipped(int x, int y, uint32_t depth, float time)
{
    NotifySpriteDrawClipped(this, x, y, depth, time);
    SpriteDrawClippedScope observerScope;
    g_calls.Count();
    DrawClippedFrames(this, x, y, depth, time);
}

void SpriteInstance::DrawClippedToned(int x, int y, uint32_t depth, float time, float scale, float blend)
{
    g_calls.Count();
    DrawClippedTonedFrames(this, x, y, depth, time, scale, blend);
}

void SpriteInstance::DrawInWorld(const float* world, float time)
{
    g_calls.Count();
    DrawInWorldFrames(this, world, time);
}

void SpriteInstance::DrawInWorldToned(const float* world, float time, float scale, float blend)
{
    g_calls.Count();
    DrawInWorldTonedFrames(this, world, time, scale, blend);
}

void SpriteInstance::DrawInWorldTonedWithExtra(const float* world, float time, float scale, float blend, uint32_t extra)
{
    g_calls.Count();
    DrawInWorldTonedExFrames(this, world, time, scale, blend, extra);
}

} // namespace obl::ui
