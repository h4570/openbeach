#include "game/ui/renderer_api.h"
#include "game/ui/sprite_anim.h"
#include "game/ui/blit.h"
#include "game/ui/sprite_instance_preview.h"
#include "game/ui/sprite.h"

#include "game/game/game_accessors.h"
#include "game/map/grid_state.h"
#include "game/ui/map_view_access.h"
#include "render_geometry.h"
#include "game/ui/sprite_instance_geometry.h"
#include "game/ui/sprite_instance_render.h"
#include "game/ui/sprite_preview.h"
#include "core/call_counter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include "game/ui/map_view_math.h"

namespace obl::ui {
namespace {

using Instance = ui::SpriteInstance;
using Anim = ui::SpriteAnim;
using Sprite = ui::Sprite;

constexpr int kTypeBuilding = render_geometry::kTypeBuilding;

test::CallCounter g_calls("ui.sprite-instance-preview");
Anim* CurrentAnim(const Instance* instance)
{
    return instance->m_sprite->m_anims[instance->m_animIndex];
}

using PixelBlitter = void (*)(
        uint32_t*, const uint8_t*, const uint8_t*, const uint16_t*, int, int, int, int, int, int, int, int, int, uint32_t, float);

uint8_t* FrameOf(const Instance* instance, Anim* anim)
{
    return anim->GetFrame(instance->m_frameIndex, instance->m_direction);
}

// Destination and source window of one blit. `left..bottom` are the clip in frame coordinates, (x, y) the already
// clipped destination origin.
struct Target {
    int x = 0, y = 0;
    int left = 0, top = 0, right = 0, bottom = 0;
    int width = 0, height = 0; // frame size (animation 0 for buildings)
};

// The first steps shared by every path: animation 0 only (a building has no other to preview), then the screen
// position and the clip. false = clipped away entirely.
bool ClipFrame(Instance* instance, const float* world, Target* t, const Anim* sizeAnim)
{
    t->left = t->top = 0;
    t->right = t->width = sizeAnim->m_width;
    t->bottom = t->height = sizeAnim->m_height;
    int screen[2];
    GetMapView()->WorldToScreen(world, screen);
    t->x = screen[0] + instance->GetDrawOffsetX();
    t->y = screen[1] + CurrentAnim(instance)->m_hotspotY;
    return obl::ui::renderer::ClipRect(game::GetRenderer(), &t->x, &t->y, &t->left, &t->top, &t->right, &t->bottom) != 0;
}

void ResetAnimation(Instance* instance)
{
    if (instance->m_animIndex != 0) {
        instance->m_animIndex = 0;
        instance->m_frameIndex = 0;
    }
}

// Depth (MapView::GetDepth, low 16 bits) of the far footprint corner and of the origin tile's centre. The footprint
// is `tilesDir0` tiles along neighbour direction 0 and `tilesDir6` along direction 6, counted from the tile under
// the world point; both start with one tile.
FootprintDepth MeasureFootprint(const float* world, int tilesDir6, int tilesDir0)
{
    map::Grid* grid = game::GetGrid();
    ui::MapView* view = GetMapView();
    const grid_math::TileXY origin = grid->WorldPointToTile(world[0], world[1]);
    uint32_t tile[2] = {origin.x, origin.y};
    for (int n = tilesDir0 - 1; n > 0; --n) {
        const grid_math::TileXY next = grid->GetNeighbour(tile[0], tile[1], 0);
        tile[0] = next.x;
        tile[1] = next.y;
    }
    for (int n = tilesDir6 - 1; n > 0; --n) {
        const grid_math::TileXY next = grid->GetNeighbour(tile[0], tile[1], 6);
        tile[0] = next.x;
        tile[1] = next.y;
    }
    const grid_math::WorldPoint farPoint = grid->TileToWorldPoint(tile[0], tile[1], 0);
    const grid_math::WorldPoint originPoint = grid->TileToWorldPoint(origin.x, origin.y, 1);
    FootprintDepth depth;
    depth.far = view->GetDepth(&farPoint.x) & 0xffff;
    depth.origin = view->GetDepth(&originPoint.x) & 0xffff;
    return depth;
}

// Both building animations count as used now (the stale-animation sweeper unloads them after 5 s without).
void StampAnimations(const Instance* instance)
{
    const float time = game::GetWorld()->m_gameTime;
    instance->m_sprite->m_anims[0]->m_lastUsedTime = time;
    instance->m_sprite->m_anims[1]->m_lastUsedTime = time;
}

float DepthStep(const Instance* instance, const FootprintDepth& depth)
{
    const uint8_t extent = instance->m_sprite->m_buildingExtra[instance->m_direction];
    return sprite_preview::DepthPerPixel(static_cast<int>(depth.origin) - static_cast<int>(depth.far), extent);
}

struct Ramp {
    bool ghost = false;
    bool zoomOut = false;
    int index() const { return (zoomOut ? 2 : 0) + (ghost ? 1 : 0); }
};

void BlitRamp(Instance* instance, const Target& t, uint32_t depth, float step, Ramp ramp)
{
    ui::Renderer* renderer = game::GetRenderer();
    const Sprite* sprite = instance->m_sprite;
    const uint16_t* palette = sprite->m_palettes[0];
    const uint8_t* depthFrame = FrameOf(instance, sprite->m_anims[1]);
    const uint8_t* colorFrame = FrameOf(instance, sprite->m_anims[0]);
    static const PixelBlitter blitters[4] = {ui::pixel_primitives::DepthRamp32, ui::pixel_primitives::DepthRampGhost32,
            ui::pixel_primitives::DepthRampZoomOut32, ui::pixel_primitives::DepthRampGhostZoomOut32};
    blitters[ramp.index()](renderer->m_workBuffer, colorFrame, depthFrame, palette, renderer->m_width, t.x, t.y, t.width, t.height, t.left,
            t.top, t.right, t.bottom, depth, step);
}

struct Outcome {
    bool drew = false;
    Target target;
    bool halved = false;
    bool flat = false, upper = false, lower = false; // branch taken (coverage counters)
    bool forwarded = false;
    int ramp = -1;
};

struct Run {
    bool draw = true; // false: plan only (no stamps, no animation step, no blit)
    bool update = true; // Alt of a non-building: step the animation
};

void DrawInWorld(Instance* instance, const float* world, float time)
{
    instance->DrawInWorld(world, time);
}

void PreviewImpl(Instance* instance, const float* world, int tilesDir6, int tilesDir0, char ghost, Run run, Outcome* out)
{
    const Sprite* sprite = instance->m_sprite;
    if (sprite->m_type != kTypeBuilding) {
        out->forwarded = true;
        if (run.draw) DrawInWorld(instance, world, game::GetWorld()->m_gameTime);
        return;
    }
    ResetAnimation(instance);
    Target t;
    if (!ClipFrame(instance, world, &t, sprite->m_anims[0])) return;
    out->drew = true;
    out->target = t;
    const FootprintDepth depth = MeasureFootprint(world, tilesDir6, tilesDir0);
    Ramp ramp;
    ramp.ghost = ghost != 0;
    ramp.zoomOut = game::GetRenderer()->m_zoomLevel == 2;
    out->halved = ramp.zoomOut;
    out->ramp = ramp.index();
    if (!run.draw) return;
    StampAnimations(instance);
    BlitRamp(instance, t, depth.far, DepthStep(instance, depth), ramp);
}

void AltImpl(Instance* instance, const float* world, int tilesDir6, int tilesDir0, float progress, char lowerPart, Run run, Outcome* out)
{
    const Sprite* sprite = instance->m_sprite;
    ui::Renderer* renderer = game::GetRenderer();
    if (sprite->m_type == kTypeBuilding) {
        ResetAnimation(instance);
        Target t;
        if (!ClipFrame(instance, world, &t, sprite->m_anims[0])) return;
        const FootprintDepth depth = MeasureFootprint(world, tilesDir6, tilesDir0);
        if (run.draw) StampAnimations(instance);
        const float step = DepthStep(instance, depth);
        sprite_preview::Window window{t.y, t.top, t.bottom};
        if (lowerPart == 0) {
            sprite_preview::ClipToUpperPart(&window, progress, t.height);
            out->upper = true;
        } else {
            sprite_preview::ClipToLowerPart(&window, progress, t.height);
            out->lower = true;
        }
        t.y = window.dstY;
        t.top = window.top;
        t.bottom = window.bottom;
        Ramp ramp;
        ramp.zoomOut = renderer->m_zoomLevel == 2;
        out->drew = true;
        out->target = t;
        out->halved = ramp.zoomOut;
        out->ramp = ramp.index();
        if (run.draw) BlitRamp(instance, t, depth.far, step, ramp);
        return;
    }

    // Any other sprite type: one ActorSelect draw at zoom 1, bottom-clipped by (progress - 1) * height.
    ResetAnimation(instance);
    out->flat = true;
    int screen[2];
    GetMapView()->WorldToScreen(world, screen);
    Target t;
    t.x = screen[0] + instance->GetDrawOffsetX();
    t.y = screen[1] + CurrentAnim(instance)->m_hotspotY;
    if (run.draw && run.update) instance->UpdateAnimNoSound(game::GetWorld()->m_gameTime);
    Anim* anim = CurrentAnim(instance);
    t.left = t.top = 0;
    t.right = t.width = anim->m_width;
    t.bottom = t.height = anim->m_height;
    if (!obl::ui::renderer::ClipRect(renderer, &t.x, &t.y, &t.left, &t.top, &t.right, &t.bottom)) return;
    const uint32_t depth = GetMapView()->GetDepth(world) & 0xffff;
    const uint8_t* frame = FrameOf(instance, anim);
    const uint16_t* palette = sprite->m_palettes[instance->m_paletteIndex];
    sprite_preview::Window window{t.y, t.top, t.bottom};
    sprite_preview::ClipFlat(&window, progress, t.height);
    t.bottom = window.bottom;
    out->drew = renderer->m_zoomLevel == 1;
    out->target = t;
    if (run.draw && out->drew)
        ui::pixel_primitives::ActorSelect(renderer->m_workBuffer, frame, palette, renderer->m_width, t.x, t.y, t.width, t.height, t.left,
                t.top, t.right, t.bottom, depth + 0x10, anim->m_hotspotY, 0, 0, 0);
}

} // namespace

FootprintDepth MeasureBuildingFootprint(const float* world, int tilesDir6, int tilesDir0)
{
    return MeasureFootprint(world, tilesDir6, tilesDir0);
}

void SpriteInstance::DrawBuildingPreview(const float* world, int tilesDir6, int tilesDir0, bool ghost)
{
    g_calls.Count();
    Outcome ignored;
    PreviewImpl(this, world, tilesDir6, tilesDir0, ghost ? 1 : 0, {}, &ignored);
}

void SpriteInstance::DrawBuildingPreviewAlt(const float* world, int tilesDir6, int tilesDir0, float progress, bool lowerPart)
{
    g_calls.Count();
    Outcome ignored;
    AltImpl(this, world, tilesDir6, tilesDir0, progress, lowerPart ? 1 : 0, {}, &ignored);
}

} // namespace obl::ui
