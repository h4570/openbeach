#include "game/game/startup_tables.h"
#include "game/ui/renderer_api.h"
#include "game/ui/blit.h"

#include "game/game/game_accessors.h"
#include "game/ui/renderer_types.h"
#include "game/ui/renderer_ops.h"
#include "core/call_counter.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace obl::ui {
namespace {
using ui::Renderer;
using ui::RendererImage;
using ui::RendererSurface;
using ui::SurfaceAt;
test::CallCounter g_image("RendererDrawImage");
test::CallCounter g_loadingBar("RendererDrawLoadingBar");
test::CallCounter g_opaque("RendererDrawImageOpaque");
test::CallCounter g_keyed("RendererDrawImageKeyed");
test::CallCounter g_masked("RendererDrawImageMasked");
test::CallCounter g_surface("RendererDrawImageSurface");
test::CallCounter g_presentWorld("RendererPresentWorld");

void World(Renderer* r)
{
    r->m_activeSurface = 0;
    auto* bits = obl::ui::renderer::GetBackBufferBits(r);
    if (!bits) return;
    const auto* surface = SurfaceAt(*r, 0);
    const int pitch = (surface->m_pitch & ~3) / 2;
    const int height = r->m_height - r->m_bottomTrim;
    if (r->m_zoomLevel == 0) {
        const uint16_t half = game::GetBlitHalfMask();
        for (int y = 0; y < height / 2; ++y) {
            auto* row = bits + y * 2 * pitch;
            const auto* src = r->m_workBuffer + y * r->m_width;
            for (int x = 0; x < r->m_width / 2; ++x) {
                const uint16_t color = static_cast<uint16_t>(src[x]);
                const uint16_t smoothed = static_cast<uint16_t>(((color & half) >> 1) * 2);
                row[x * 2] = row[pitch + x * 2] = smoothed;
                row[x * 2 + 1] = row[pitch + x * 2 + 1] = color;
            }
        }
    } else {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < r->m_width; ++x) bits[y * pitch + x] = static_cast<uint16_t>(r->m_workBuffer[y * r->m_width + x]);
    }
    r->m_surfaceLocked = 0;
}

void PresentWorldImpl(Renderer* r)
{
    World(r);
    g_presentWorld.Count();
}

void InitPixelFormatGlobalsImpl(void* scene, int width, int format)
{
    const bool rgb555 = format != 0;
    const uint16_t quarter = rgb555 ? 0x739c : 0xe79c;
    const uint16_t half = rgb555 ? 0x7bde : 0xf7de;
    game::SetBlitQuarterMask(quarter);
    game::SetBlitHalfMask(half);
    game::SetPixelRedMask(rgb555 ? 0x7c00 : 0xf800);
    game::SetPixelGreenMask(rgb555 ? 0x3e0 : 0x7e0);
    game::SetPixelBlueMask(0x1f);
    game::SetPixelRedShift(rgb555 ? 10 : 11);
    game::SetPixelGreenShift(rgb555 ? 5 : 6);
    game::SetPixelGreenMax(rgb555 ? 0x1f : 0x3f);
    game::SetBlitKeylineColor(rgb555 ? 0x7fff : 0xffff);
    game::SetBlitWaterQuarterWhite(quarter >> 2);
    game::SetBlitWaterHalfWhite(half >> 1);
    game::SetBlitWaterThreeQuarterWhite((quarter >> 2) + (half >> 1));
    game::SetBlitPixelFormat(format);
    game::SetBlitSceneBuffer(scene);
    game::SetBlitSceneWidth(width);
}

void Image(Renderer* r, const int* position, const int* rect, const RendererImage* image, int mode, uint32_t flags, uint32_t mask)
{
    int x = position[0], y = position[1], l = rect[0], t = rect[1], right = rect[2], bottom = rect[3];
    if (!right || !bottom) {
        right = l + image->m_width;
        bottom = t + image->m_height;
    }
    if (!obl::ui::renderer::ClipRect(r, &x, &y, &l, &t, &right, &bottom)) return;
    const int width = right - l, height = bottom - t;

    if (mode == 0) {
        if (!(flags & 1)) {
            ui::pixel_primitives::Bitmap8Opaque32(
                    image->m_pixels, image->m_width, r->m_workBuffer, r->m_width, l, t, x, y, width, height, image->m_palette16);
            g_opaque.Count();
        } else if (static_cast<uint32_t>(y + height) < static_cast<uint32_t>(r->m_height)) {
            ui::pixel_primitives::Bitmap8Keyed32(
                    image->m_pixels, image->m_width, r->m_workBuffer, r->m_width, l, t, x, y, width, height, image->m_palette16);
            g_keyed.Count();
        }
    } else if (mode == 10) {
        ui::pixel_primitives::Bitmap8KeyedMasked32(image->m_pixels, image->m_width, r->m_workBuffer, r->m_width, l, t, x, y, width, height,
                image->m_palette16, static_cast<uint16_t>(mask));
        g_masked.Count();
    } else if (mode == 11) {
        auto* bits = obl::ui::renderer::GetBackBufferBits(r);
        if (bits) {
            // This mode deliberately draws at surface (0,0), regardless of the clipped screen position.
            ui::pixel_primitives::Bitmap8Keyed16(image->m_pixels, image->m_width, bits, SurfaceAt(*r, r->m_activeSurface)->m_pitch, l, t, 0,
                    0, width, height, image->m_palette16);
            obl::ui::renderer::UnlockSurface(r);
            g_surface.Count();
        }
    }
}

// The accumulators stay in double: the loop keeps them in registers at 53-bit precision, and narrowing each
// step to float changes integer boundaries for small fractional increments. Each channel is truncated toward zero to
// int64 (truncating) and its low 32 bits are stored.
void LoadingChannels(int columns, const ui::RendererLoadingGradient& gradient, int* output)
{
    double red = 11, green = 105, blue = 150;
    const double dr = gradient.m_redStep, dg = gradient.m_greenStep, db = gradient.m_blueStep;
    for (int column = 0; column < columns; ++column) {
        red += dr;
        output[column * 3] = static_cast<int32_t>(static_cast<int64_t>(red));
        green += dg;
        output[column * 3 + 1] = static_cast<int32_t>(static_cast<int64_t>(green));
        blue += db;
        output[column * 3 + 2] = static_cast<int32_t>(static_cast<int64_t>(blue));
    }
}

void LoadingPixels(Renderer* r, float progress)
{
    const int columns = static_cast<int>(static_cast<double>(progress) * 697.0);
    if (columns <= 0) return;
    std::vector<int> channels(static_cast<size_t>(columns) * 3);
    LoadingChannels(columns, game::LoadingGradient(), channels.data());
    for (int x = 0; x < columns; ++x) {
        const uint32_t color = renderer_ops::PackColor(r->m_rgb555 != 0, channels[x * 3], channels[x * 3 + 1], channels[x * 3 + 2]);
        for (int y = 551; y < 571; ++y) r->m_workBuffer[y * r->m_width + 51 + x] = color;
    }
}

void DrawImageImpl(Renderer* r, const int* pos, const int* rect, const RendererImage* image, int mode, uint32_t flags, uint32_t mask)
{
    Image(r, pos, rect, image, mode, flags, mask);
    g_image.Count();
}

void DrawLoadingBarImpl(Renderer* r, float progress)
{
    LoadingPixels(r, progress);
    obl::ui::renderer::PresentWorld(r);
    obl::ui::renderer::Present(r);
    g_loadingBar.Count();
}

} // namespace

void ui::renderer::PresentWorld(void* r)
{
    return PresentWorldImpl(static_cast<Renderer*>(r));
}

void ui::renderer::DrawImage(void* r, const int* pos, const int* rect, const void* image, int mode, uint32_t flags, uint32_t mask)
{
    return DrawImageImpl(static_cast<Renderer*>(r), pos, rect, static_cast<const RendererImage*>(image), mode, flags, mask);
}

void ui::renderer::DrawLoadingBar(void* r, float progress)
{
    return DrawLoadingBarImpl(static_cast<Renderer*>(r), progress);
}

void ui::renderer::InitPixelFormatGlobals(void* scene, int width, int format)
{
    return InitPixelFormatGlobalsImpl(scene, width, format);
}

} // namespace obl::ui
