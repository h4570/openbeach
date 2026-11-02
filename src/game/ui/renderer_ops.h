#pragma once

#include "game/ui/blit.h"
#include "game/ui/renderer_types.h"
#include "core/rect.h"

#include <cstddef>
#include <cstdint>

// The small Renderer operations of the `display` module: pixel packing, the sprite clip, the rectangle
// fills, the single pixel and the fade of the loading screen. Pure functions over values and buffers; the functions in
// game/renderer_helpers gather the arguments from the Renderer object. The dirty rectangle list is renderer_dirty.h.
namespace obl::renderer_ops {

using screen::Rect;

// ui::renderer::PackColor: r, g, b are the low 8 bits of 0..255 channels; only the bits the format keeps are used (r and g are
// masked, not shifted down first, so a value above 255 leaks into the neighbouring channel as in the game).
uint16_t PackColor(bool rgb555, uint32_t r, uint32_t g, uint32_t b);

// ui::renderer::SetZoomLevel: level 0 zooms in (scale 2), 1 is normal, 2 zooms out (0.5). Any other level leaves the scale alone.
// Returns false when the scale is not written.
bool ZoomScale(int level, float& scale);

// The fields ui::renderer::ClipRect reads: the scene view (position and size), the draw mode and the dirty list switch.
struct ClipView {
    int x = 0, y = 0, width = 0, height = 0; // scene view origin and size
    bool directMode = false; // drawing into the locked back buffer
    int zoomLevel = 1; // zoom level of the view
    bool recordDirty = false; // dirty-rectangle recording is enabled and active
};

struct ClipResult {
    bool visible = false;
    bool addDirty = false; // the clipped destination rectangle goes to AddDirtyRect
    Rect dirty{};
};

// ui::renderer::ClipRect: clips the sprite rectangle {left, top, right, bottom} drawn at (x, y) to the view. The destination
// edge is the view edge scaled by the zoom (x2 zoomed out, /2 zoomed in; unscaled in direct mode and at zoom 1). `visible` is false
// when the sprite lies wholly outside; the outputs are written only when it is not rejected by the first four comparisons.
// A zoom level outside 0..2 (never set by the game) is treated as 1.
ClipResult ClipRect(const ClipView& view, int& x, int& y, int& left, int& top, int& right, int& bottom);

// The renderer's visibility test: the sprite frame at (x, y) with source offset (srcX, srcY) and size width x height against the
// view. The far edge is the view edge unscaled at zoom 1 and in direct mode without the overlay flag, x2 at zoom 2 and /2 at
// zoom 0. Any other zoom leaves the game comparing the *address* of its `x` argument in place of both edges: `strayEdge` is that value.
struct VisibleView {
    int x = 0, y = 0, width = 0, height = 0; // scene view origin and size
    int zoomLevel = 1; // zoom level of the view
    bool directMode = false; // drawing into the locked back buffer
    bool overlay = false; // m_renderVisibleObjects
};
bool IsRectVisible(const VisibleView& view, int x, int y, int srcX, int srcY, uint32_t width, uint32_t height, int strayEdge);

// The overlap of two {left, top, right, bottom} rectangles. Touching edges count as overlap (only a strict
// gap rejects); `out` is written only when they overlap.
bool IntersectRects(const Rect& a, const Rect& b, Rect& out);

// ui::renderer::FillRect: the rectangle {left, top, right, bottom} cut to 0..width / 0..height. A rectangle that is empty after
// the cut has width or height <= 0 and fills nothing.
struct FillSpan {
    int x = 0, y = 0, width = 0, height = 0;
};
FillSpan ClipFill(int sceneWidth, int sceneHeight, const Rect& rect);

// The scene buffer keeps its depth word (high 16 bits) and takes the colour into the low 16 bits.
void FillScene(uint32_t* scene, int sceneWidth, const FillSpan& span, uint16_t color);
// The locked back buffer: `rowStart` is the first pixel of the span, `pitchPixels` the distance between rows.
void FillBackBuffer(uint16_t* rowStart, uint32_t pitchPixels, const FillSpan& span, uint16_t color);

// ui::renderer::ApplyFade on `rows` rows of `width` pixels. Modes: 0 keeps the pixel; 1 is half plus quarter of the pixel plus the
// quarter white of g_BlitWaterQuarterWhite; 2 half the pixel plus half white; 3 a quarter of the pixel plus three quarter white; 4 white.
// (Half / quarter are taken through g_BlitHalfMask / g_BlitQuarterMask.) Other modes leave the pixel untouched (the game stores
// whatever its register holds; the game passes 0..4).
void ApplyFade(uint16_t* pixels, int width, int rows, uint8_t mode, blit::Masks masks, blit::WaterColors colors);

// The pixel `index` of `bits`. The game computes the index in 32-bit unsigned arithmetic, so an origin left of or above
// the surface wraps to a pixel before `bits`; read as a signed byte offset the same address comes out on 32 and 64 bits.
template<typename Pixel> Pixel* PixelAt(Pixel* bits, uint32_t index)
{
    return bits + static_cast<int32_t>(index * 2u) / 2;
}

// ui::renderer::ClearSurface (windowed branch): `count` pixels (pitch / 2 * height, the rows are contiguous) set to the colour key.
void ClearRows(uint16_t* bits, size_t count, uint16_t key);

// The copy loop of the renderer's surface blits (ui::renderer::BlitSurface and its keyed variants, windowed branch): `rows` rows of `width` pixels from `src`
// to `dst`, skipping the source pixels equal to `key`. The strides are in pixels; the source advances by `srcStride` per row (the whole
// pitch for a full surface, the same for a sub-rectangle whose `width` is narrower). Nothing is clipped, the caller checks the bounds.
// `rows <= 0` copies nothing (the game would loop on a negative count; the game never passes one).
void CopyKeyed(const uint16_t* src, ptrdiff_t srcStride, uint16_t* dst, ptrdiff_t dstStride, uint32_t width, int rows, uint16_t key);

// ui::renderer::AllocSurface (windowed branch): the byte size of the pixel block and the pitch of a w x h surface, both computed
// in 32-bit unsigned arithmetic so they wrap.
constexpr uint32_t SurfaceBlockBytes(int width, int height)
{
    return static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 2u;
}
constexpr int SurfacePitch(int width)
{
    return static_cast<int>(static_cast<uint32_t>(width) * 2u);
}

// ui::renderer::IsModeSupported: the index into Renderer::m_modeSupported of a display width, -1 (unsupported) for any other.
constexpr int ModeSupportedIndex(int width)
{
    return width == 640 ? 0 : width == 800 ? 1 : width == 1024 ? 2 : width == 1280 ? 3 : -1;
}

// One frame of ui::renderer::Present's screen fade: advances `fade` and returns the ui::renderer::ApplyFade mode to draw this frame,
// or -1 while the fade is off. The level climbs 0..4 one step every third frame, holds mode 4 for 20 frames, then climbs back down and
// switches itself off at 0.
int StepFade(ui::RendererFade& fade);

} // namespace obl::renderer_ops
