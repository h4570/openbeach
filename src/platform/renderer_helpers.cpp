// The small Renderer operations (docs/architecture/presentation-render.md): SetViewport, SetGuiBarHeight,
// SetZoomLevel, GetZoomLevel, ClearLightBuffer, PackColor, PutPixel, FillRect / FillRectBySize (with the fill observer,
// fill_observer.h), ClipRect, AddDirtyRect / PopDirtyRect and ApplyFade over renderer_ops and renderer_dirty.
#include "game/ui/renderer_api.h"

#include "game/game/game_accessors.h"
#include "game/ui/fill_observer.h"
#include "game/ui/renderer_types.h"
#include "core/rect.h"
#include "platform/renderer_backbits.h"
#include "platform/renderer_dirty.h"
#include "game/ui/renderer_ops.h"

#include <cstdint>
#include <cstring>

// The renderer's small operations (docs/architecture/presentation.md): viewport, GUI bar height, zoom, light buffer, colors,
// pixels, rectangles, clipping, dirty rectangles and the fade.
namespace obl::ui {
namespace {

using screen::Rect;
using ui::Renderer;

// ---- the surface the direct mode draws on ----------------------------------------------------------------------------------------

// The surface record of the current surface (Renderer::m_activeSurface).
const ui::RendererSurface* CurrentSurface(const Renderer& r)
{
    return ui::SurfaceAt(r, r.m_activeSurface);
}

// The pixel (x, y) of the locked back buffer; the index arithmetic wraps at 32 bits (unsigned, so negative offsets wrap around).
uint16_t* BackPixel(uint16_t* bits, const ui::RendererSurface* surface, int x, int y)
{
    const uint32_t pitchPixels = static_cast<uint32_t>(surface->m_pitch) >> 1;
    const auto originX = static_cast<uint32_t>(surface->m_originX);
    const auto originY = static_cast<uint32_t>(surface->m_originY);
    const uint32_t index = pitchPixels * (static_cast<uint32_t>(y) - originY) - originX + static_cast<uint32_t>(x);
    return renderer_ops::PixelAt(bits, index);
}

// ---- the functions ---------------------------------------------------------------------------------------------------------------------

void SetViewport(Renderer& r, const int* rect)
{
    std::memcpy(r.m_view, rect, sizeof(r.m_view));
}
void SetGuiBarHeight(Renderer& r, int height)
{
    r.m_bottomTrim = height;
}

void SetZoomLevel(Renderer& r, int level)
{
    r.m_zoomLevel = level;
    r.m_lightsDirty = 1;
    r.m_lightsDirtyFrom = 0;
    renderer_ops::ZoomScale(level, r.m_zoomScale);
}

void ClearLightBuffer(Renderer& r)
{
    r.m_lightsDirty = 1;
    r.m_lightsDirtyFrom = 0;
    std::memset(r.m_lightBuffer, 0, static_cast<size_t>(static_cast<uint32_t>(r.m_height) * static_cast<uint32_t>(r.m_width)));
}

void PutPixel(Renderer& r, int x, int y, uint16_t color)
{
    if (!r.m_directMode) {
        uint32_t& word = r.m_workBuffer[r.m_width * y + x];
        word = (word & 0xffff0000u) + color;
        return;
    }
    uint16_t* bits = platform::BackBits(r);
    if (!bits) return;
    *BackPixel(bits, CurrentSurface(r), x, y) = color;
    r.m_surfaceLocked = 0;
}

void FillRect(Renderer& r, const Rect& rect, uint16_t color)
{
    const renderer_ops::FillSpan span = renderer_ops::ClipFill(r.m_width, r.m_height, rect);
    if (!r.m_directMode) {
        renderer_ops::FillScene(r.m_workBuffer, r.m_width, span, color);
        return;
    }
    uint16_t* bits = platform::BackBits(r);
    if (!bits) return;
    const ui::RendererSurface* surface = CurrentSurface(r);
    renderer_ops::FillBackBuffer(BackPixel(bits, surface, span.x, span.y), static_cast<uint32_t>(surface->m_pitch) >> 1, span, color);
    r.m_surfaceLocked = 0;
}

renderer_ops::ClipView ViewOf(const Renderer& r)
{
    renderer_ops::ClipView view;
    view.x = r.m_view[0];
    view.y = r.m_view[1];
    view.width = r.m_view[2];
    view.height = r.m_view[3];
    view.directMode = r.m_directMode != 0;
    view.zoomLevel = r.m_zoomLevel;
    view.recordDirty = r.m_trackDirtyRects != 0 && r.m_lightsDirty == 0 && r.m_clipRecordsDirty != 0;
    return view;
}

blit::Masks CurrentMasks()
{
    return {game::GetBlitHalfMask(), game::GetBlitQuarterMask()};
}
blit::WaterColors CurrentColors()
{
    return {game::GetBlitWaterQuarterWhite(), game::GetBlitWaterHalfWhite(), game::GetBlitWaterThreeQuarterWhite()};
}

// ---- the dirty rectangle list ----------------------------------------------------------------------------------------------------

struct DirtyList {
    Renderer& r;
    size_t size() const { return r.m_dirtyRects.size(); }
    Rect& at(size_t i) const { return r.m_dirtyRects[i]; }
    void swap_remove(size_t i)
    {
        at(i) = at(size() - 1);
        r.m_dirtyRects.pop_back();
    }
    void push_back(const Rect& rect) { r.m_dirtyRects.push_back(rect); }
};

bool PopDirtyRect(Renderer& r, Rect* out)
{
    if (r.m_dirtyRects.empty()) return false;
    *out = r.m_dirtyRects.back();
    r.m_dirtyRects.pop_back();
    return true;
}

// ---- operations ------------------------------------------------------------------------------------------------------------------------

void SetViewportImpl(Renderer* r, const int* rect)
{
    SetViewport(*r, rect);
}

void SetGuiBarHeightImpl(Renderer* r, int height)
{
    SetGuiBarHeight(*r, height);
}

void SetZoomLevelImpl(Renderer* r, int level)
{
    SetZoomLevel(*r, level);
}

void ClearLightBufferImpl(Renderer* r)
{
    ClearLightBuffer(*r);
}

int GetZoomLevelImpl()
{
    return game::GetRenderer()->m_zoomLevel;
}

uint16_t PackColorImpl(Renderer* r, uint32_t red, uint32_t green, uint32_t blue)
{
    return renderer_ops::PackColor(r->m_rgb555 != 0, red, green, blue);
}

void PutPixelImpl(Renderer* r, int x, int y, uint32_t color)
{
    PutPixel(*r, x, y, static_cast<uint16_t>(color));
}

void FillRectImpl(Renderer* r, const int* rect, uint32_t color)
{
    NotifyFill(r, false, rect, static_cast<uint16_t>(color));
    FillRect(*r, {rect[0], rect[1], rect[2], rect[3]}, static_cast<uint16_t>(color));
}

void FillRectXYWHImpl(Renderer* r, const int* rect, uint32_t color)
{
    NotifyFill(r, true, rect, static_cast<uint16_t>(color));
    FillRect(*r, {rect[0], rect[1], rect[2] + rect[0], rect[3] + rect[1]}, static_cast<uint16_t>(color));
}

void ApplyFadeImpl(uint16_t* pixels, int width, int rows, uint32_t mode)
{
    renderer_ops::ApplyFade(pixels, width, rows, static_cast<uint8_t>(mode), CurrentMasks(), CurrentColors());
}

void AddDirtyRectImpl(Renderer* r, int left, int top, int right, int bottom)
{
    DirtyList list{*r};
    renderer_ops::AddDirtyRect(list, left, top, right, bottom);
}

bool PopDirtyRectImpl(Renderer* r, Rect* out)
{
    return PopDirtyRect(*r, out);
}

char ClipRectImpl(Renderer* r, int* x, int* y, int* left, int* top, int* right, int* bottom)
{
    const renderer_ops::ClipResult result = renderer_ops::ClipRect(ViewOf(*r), *x, *y, *left, *top, *right, *bottom);
    if (result.addDirty) {
        DirtyList list{*r};
        renderer_ops::AddDirtyRect(list, result.dirty.left, result.dirty.top, result.dirty.right, result.dirty.bottom);
    }
    return result.visible ? 1 : 0;
}

} // namespace

void ui::renderer::SetViewport(void* r, const int* rect)
{
    return SetViewportImpl(static_cast<Renderer*>(r), rect);
}

void ui::renderer::SetGuiBarHeight(void* r, int height)
{
    return SetGuiBarHeightImpl(static_cast<Renderer*>(r), height);
}

void ui::renderer::SetZoomLevel(void* r, int level)
{
    return SetZoomLevelImpl(static_cast<Renderer*>(r), level);
}

int ui::renderer::GetZoomLevel()
{
    return GetZoomLevelImpl();
}

void ui::renderer::ClearLightBuffer(void* r)
{
    return ClearLightBufferImpl(static_cast<Renderer*>(r));
}

uint16_t ui::renderer::PackColor(void* r, uint32_t red, uint32_t green, uint32_t blue)
{
    return PackColorImpl(static_cast<Renderer*>(r), red, green, blue);
}

void ui::renderer::PutPixel(void* r, int x, int y, uint32_t color)
{
    return PutPixelImpl(static_cast<Renderer*>(r), x, y, color);
}

void ui::renderer::FillRect(void* r, const int* rect, uint32_t color)
{
    return FillRectImpl(static_cast<Renderer*>(r), rect, color);
}

void ui::renderer::FillRectBySize(void* r, const int* rect, uint32_t color)
{
    return FillRectXYWHImpl(static_cast<Renderer*>(r), rect, color);
}

char ui::renderer::ClipRect(void* r, int* x, int* y, int* left, int* top, int* right, int* bottom)
{
    return ClipRectImpl(static_cast<Renderer*>(r), x, y, left, top, right, bottom);
}

void ui::renderer::AddDirtyRect(void* r, int left, int top, int right, int bottom)
{
    return AddDirtyRectImpl(static_cast<Renderer*>(r), left, top, right, bottom);
}

bool ui::renderer::PopDirtyRect(void* r, Rect* out)
{
    return PopDirtyRectImpl(static_cast<Renderer*>(r), out);
}

void ui::renderer::ApplyFade(uint16_t* pixels, int width, int rows, uint32_t mode)
{
    return ApplyFadeImpl(pixels, width, rows, mode);
}

} // namespace obl::ui
