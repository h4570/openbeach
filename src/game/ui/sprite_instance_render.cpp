#include "game/ui/renderer_api.h"
#include "game/ui/sprite_anim.h"
#include "game/ui/blit.h"
#include "game/ui/sprite_instance_render.h"

#include "game/game/game_accessors.h"
#include "game/ui/sprite.h"
#include "game/ui/sprite_draw.h"
#include "game/ui/sprite_observer.h"
#include "core/call_counter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace obl::ui {
namespace {

using Instance = ui::SpriteInstance;
using Anim = ui::SpriteAnim;
using sprite_draw::Blitter;

// Call counters (`call-report ui.sprite-instance`).
test::CallCounter g_calls("ui.sprite-instance");

struct Frame {
    Anim* anim;
    const uint8_t* rle;
    const uint16_t* palette;
    int width, height;
};

Frame Gather(Instance* instance)
{
    Anim* anim = instance->m_sprite->m_anims[instance->m_animIndex];
    auto* rle = anim->GetFrame(instance->m_frameIndex, instance->m_direction);
    const uint16_t* palette = instance->m_sprite->m_palettes[instance->m_paletteIndex];
    if (instance->m_overridePalette) palette = instance->m_overridePalette;
    return {anim, rle, palette, anim->m_width, anim->m_height};
}

Blitter ChooseFor(const Instance* instance, const Anim* anim, const ui::Renderer* renderer)
{
    return sprite_draw::Choose(
            {instance->m_sprite->m_type, instance->m_animIndex, anim->m_name, renderer->m_directMode != 0, renderer->m_zoomLevel});
}

void Draw(Instance* instance, int x, int y, int left, int top, int right, int bottom, uint32_t depth)
{
    ui::Renderer* renderer = game::GetRenderer();
    const Frame f = Gather(instance);
    uint32_t* dst = renderer->m_workBuffer;
    const int pitch = renderer->m_width;
    const Blitter blitter = ChooseFor(instance, f.anim, renderer);
    switch (blitter) {
    case Blitter::Ui32: ui::pixel_primitives::Ui32(dst, f.rle, f.palette, pitch, x, y, f.width, f.height, left, top, right, bottom); break;
    case Blitter::Ui16Direct: {
        if (auto* bits = obl::ui::renderer::GetBackBufferBits(renderer)) {
            const uint32_t pitchBytes = obl::ui::renderer::GetSurfacePitch(renderer);
            int origin[2];
            obl::ui::renderer::GetSurfaceOrigin(renderer, origin);
            ui::pixel_primitives::Ui16(
                    bits, f.rle, f.palette, pitchBytes >> 1, x - origin[0], y - origin[1], f.width, f.height, left, top, right, bottom);
            obl::ui::renderer::UnlockSurface(renderer);
        }
        break;
    }
    case Blitter::Type2:
        ui::pixel_primitives::Type2(dst, f.rle, f.palette, pitch, x, y, f.width, f.height, left, top, right, bottom);
        break;
    case Blitter::Actor:
        ui::pixel_primitives::ActorSelect(dst, f.rle, f.palette, pitch, x, y, f.width, f.height, left, top, right, bottom, depth,
                f.anim->m_hotspotY, instance->m_lit, instance->m_actorRenderOption, instance->m_keyline);
        break;
    case Blitter::Shadow:
    case Blitter::ShadowZoomOut:
        (blitter == Blitter::Shadow ? ui::pixel_primitives::Shadow32 : ui::pixel_primitives::ShadowZoomOut32)(
                dst, f.rle, f.palette, pitch, x, y, f.width, f.height, left, top, right, bottom, depth);
        break;
    case Blitter::Translucent:
        ui::pixel_primitives::TranslucentSelect(dst, f.rle, f.palette, pitch, x, y, f.width, f.height, left, top, right, bottom);
        break;
    case Blitter::Type12:
    case Blitter::Type12ZoomOut:
        (blitter == Blitter::Type12 ? ui::pixel_primitives::DepthType12Unscaled32 : ui::pixel_primitives::DepthType12ZoomOut32)(
                dst, f.rle, f.palette, pitch, x, y, f.width, f.height, left, top, right, bottom, depth + sprite_draw::kType12DepthBias);
        break;
    case Blitter::None: break;
    }
    instance->m_overridePalette = nullptr;
}

} // namespace

// Everything one draw may touch: from the destination origin through the last row, plus a guard pixel. Whole
Region TouchedRegion(const ui::Renderer& r, int x, int y, int width, int height, bool half)
{
    const long long pitch = r.m_width;
    const long long ox = half ? x / 2 : x, oy = half ? y / 2 : y;
    const long long rows = half ? height / 2 + 2 : height;
    long long start = oy * pitch + ox;
    long long end = start + (rows - 1) * pitch + width + 1;
    start = std::max(start, 0ll);
    end = std::min(end, pitch * r.m_height);
    return end > start ? Region{static_cast<size_t>(start), static_cast<size_t>(end - start)} : Region{};
}

void SpriteInstance::Draw(int x, int y, int left, int top, int right, int bottom, uint32_t depth)
{
    NotifySpriteDraw(this, x, y, left, top, right, bottom, depth);
    g_calls.Count();
    ::obl::ui::Draw(this, x, y, left, top, right, bottom, depth);
}

} // namespace obl::ui
