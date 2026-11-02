#include "game/ui/renderer_api.h"

#include "game/game/game_accessors.h"
#include "game/ui/renderer_types.h"
#include "renderer_backbits.h"
#include "game/ui/renderer_ops.h"

#include <cstdint>
#include <cstring>

// The surface records of Renderer::m_surfaces (docs/architecture/presentation.md): the active surface (m_activeSurface), its pitch,
// origin and pixels, clearing, the keyed blits and the record life cycle. Every record owns a plain heap block of pixels; nothing is
// ever lost or locked, so unlocking only ends the hand-out, a surface is never lost and restoring has nothing to do. The blits skip the
// colour key m_greenMask and do not clip on the right or bottom (the caller keeps inside the destination); BlitSurface and the
// rectangle blit make surface 0 the active one, the keyed blit the destination.
namespace obl::ui {
namespace {

using ui::Renderer;
using ui::RendererSurface;
using ui::SurfaceAt;

// ---- the functions ---------------------------------------------------------------------------------------------------------------------

void SetActive(Renderer& r, int index)
{
    r.m_activeSurface = index;
}

uint32_t GetPitch(const Renderer& r)
{
    return static_cast<uint32_t>(SurfaceAt(r, r.m_activeSurface)->m_pitch);
}

void Unlock(Renderer& r)
{
    r.m_surfaceLocked = 0;
}

void GetOrigin(const Renderer& r, int* origin)
{
    const RendererSurface* surface = SurfaceAt(r, r.m_activeSurface);
    origin[0] = surface->m_originX;
    origin[1] = surface->m_originY;
}

void SetOrigin(Renderer& r, int index, int x, int y)
{
    RendererSurface* surface = SurfaceAt(r, index);
    surface->m_originX = x;
    surface->m_originY = y;
}

// True when the pixels were written (false: already cleared).
bool Clear(Renderer& r, int index)
{
    RendererSurface* surface = SurfaceAt(r, index);
    if (surface->m_cleared) return false;
    const size_t count = static_cast<size_t>(static_cast<uint32_t>(surface->m_pitch) >> 1) * static_cast<uint32_t>(surface->m_height);
    renderer_ops::ClearRows(surface->m_bits, count, r.m_greenMask);
    surface->m_cleared = 1;
    return true;
}

// The destination of a blit: pixel (x, y) of the active surface's pixels `bits`; the index arithmetic wraps at 32 bits.
uint16_t* DestinationPixel(const Renderer& r, uint16_t* bits, int x, int y, uint32_t& pitchWords)
{
    pitchWords = static_cast<uint32_t>(SurfaceAt(r, r.m_activeSurface)->m_pitch) >> 1;
    const uint32_t index = pitchWords * static_cast<uint32_t>(y) + static_cast<uint32_t>(x);
    return renderer_ops::PixelAt(bits, index);
}

// BlitSurface (active = 0) and BlitSurfaceKeyed (active = the destination): the whole source surface at (x, y). False when rejected.
bool Blit(Renderer& r, int x, int y, int active, int source)
{
    if (x < 0 || y < 0) return false;
    const RendererSurface* src = SurfaceAt(r, source);
    const uint32_t srcWords = static_cast<uint32_t>(src->m_pitch) >> 1;
    const uint16_t* srcBits = src->m_bits;
    const int rows = src->m_height;
    r.m_activeSurface = active;
    uint16_t* bits = platform::BackBits(r);
    if (!bits) return true;
    uint32_t dstWords;
    uint16_t* dst = DestinationPixel(r, bits, x, y, dstWords);
    renderer_ops::CopyKeyed(srcBits, srcWords, dst, dstWords, srcWords, rows, r.m_greenMask);
    r.m_surfaceLocked = 0;
    return true;
}

// BlitSurfaceRect: the source rectangle {l, t, r, b} of `source` at dstXY on the back buffer. False when rejected.
bool BlitSurfaceRect(Renderer& r, const int* dstXY, const int* rect, int source)
{
    const int left = rect[0], top = rect[1], right = rect[2], bottom = rect[3];
    if (!(left <= right && -1 < right) || !(top <= bottom)) return false;
    const RendererSurface* src = SurfaceAt(r, source);
    const uint32_t srcWords = static_cast<uint32_t>(src->m_pitch) >> 1;
    const auto* srcBits = renderer_ops::PixelAt(src->m_bits, static_cast<uint32_t>(top) * srcWords);
    r.m_activeSurface = 0;
    uint16_t* bits = platform::BackBits(r);
    if (!bits) return true;
    uint32_t dstWords;
    uint16_t* dst = DestinationPixel(r, bits, dstXY[0], dstXY[1], dstWords);
    renderer_ops::CopyKeyed(srcBits + left, srcWords, dst, dstWords, static_cast<uint32_t>(right - left), bottom - top, r.m_greenMask);
    r.m_surfaceLocked = 0;
    return true;
}

// ---- surface life cycle ----------------------------------------------------------------------------------------------------------

// AllocSurface: a fresh pixel block of w * h * 2 bytes owned by the record (the flags are ignored).
void Alloc(RendererSurface& record, int width, int height)
{
    record.m_bits = static_cast<uint16_t*>(::operator new(renderer_ops::SurfaceBlockBytes(width, height)));
    record.m_width = width;
    record.m_height = height;
    record.m_pitch = renderer_ops::SurfacePitch(width);
    record.m_cleared = 0;
}

// CreateSurface: the first free record is taken over, otherwise a new one is appended; returns the index.
int Create(Renderer& r, int width, int height)
{
    const size_t count = r.m_surfaces.size();
    for (size_t i = 0; i < count; ++i) {
        RendererSurface& record = *SurfaceAt(r, static_cast<int>(i));
        if (!record.m_inUse) {
            record.m_inUse = 1;
            Alloc(record, width, height);
            return static_cast<int>(i);
        }
    }
    auto* record = new RendererSurface{};
    record->m_inUse = 1;
    r.m_surfaces.push_back(record);
    Alloc(*record, width, height);
    return static_cast<int>(count);
}

// ReleaseSurface: the record stays in the vector for CreateSurface to reuse and gives its block back.
void Release(Renderer& r, int index)
{
    RendererSurface* record = SurfaceAt(r, index);
    if (!record->m_inUse) return;
    ::operator delete(record->m_bits);
    record->m_bits = nullptr;
    record->m_inUse = 0;
    record->m_width = 0;
    record->m_height = 0;
    record->m_pitch = 0;
}

// ResizeSurface: a different size gets a new block (the contents are not kept); true when it did.
bool Resize(Renderer& r, int index, int width, int height)
{
    RendererSurface* record = SurfaceAt(r, index);
    if (record->m_width == width && record->m_height == height) return false;
    ::operator delete(record->m_bits);
    Alloc(*record, width, height);
    return true;
}

// GetBackBufferBits: the pixels of the active surface, which count as handed out and no longer as cleared.
uint16_t* GetBits(Renderer& r)
{
    RendererSurface* record = SurfaceAt(r, r.m_activeSurface);
    record->m_cleared = 0;
    r.m_surfaceLocked = 1;
    return record->m_bits;
}

// ---- operations -----------------------------------------------------------------------------------------------------------------------

void SetActiveSurfaceImpl(Renderer* r, int index)
{
    SetActive(*r, index);
}

uint32_t GetSurfacePitchImpl(Renderer* r)
{
    return GetPitch(*r);
}

void UnlockSurfaceImpl(Renderer* r)
{
    Unlock(*r);
}

void GetSurfaceOriginImpl(Renderer* r, int* origin)
{
    GetOrigin(*r, origin);
}

void SetSurfaceOriginImpl(Renderer* r, int index, int x, int y)
{
    SetOrigin(*r, index, x, y);
}

bool IsSurfaceLostImpl(Renderer*, int)
{
    return false;
}

void RestoreSurfaceImpl(Renderer*, int) {}

// Only the system-memory renderer is created.
void ClearSurfaceImpl(Renderer* r, int index)
{
    Clear(*r, index);
}

void BlitSurfaceImpl(Renderer* r, int x, int y, int source)
{
    Blit(*r, x, y, 0, source);
}

void BlitSurfaceKeyedImpl(Renderer* r, int x, int y, int destination, int source)
{
    Blit(*r, x, y, destination, source);
}

void BlitSurfaceRectImpl(Renderer* r, const int* dstXY, const int* rect, int source)
{
    BlitSurfaceRect(*r, dstXY, rect, source);
}

// System-memory surface operations.
uint16_t* GetBackBufferBitsImpl(Renderer* r)
{
    return GetBits(*r);
}

int CreateSurfaceImpl(Renderer* r, int width, int height, int flags)
{
    return Create(*r, width, height);
}

void AllocSurfaceImpl(Renderer* r, int width, int height, void* record, int flags)
{
    Alloc(*static_cast<RendererSurface*>(record), width, height);
}

void ReleaseSurfaceImpl(Renderer* r, int index)
{
    Release(*r, index);
}

bool ResizeSurfaceImpl(Renderer* r, int index, int width, int height)
{
    return Resize(*r, index, width, height);
}

// Plain heap surfaces are never lost.
void RecreateSurfaceImpl(Renderer* r, int index) {}

void RestoreLostSurfacesImpl(Renderer* r) {}

} // namespace

uint16_t* ui::renderer::GetBackBufferBits(void* r)
{
    return GetBackBufferBitsImpl(static_cast<Renderer*>(r));
}

int ui::renderer::CreateSurface(void* r, int width, int height, int flags)
{
    return CreateSurfaceImpl(static_cast<Renderer*>(r), width, height, flags);
}

void ui::renderer::AllocSurface(void* r, int width, int height, void* record, int flags)
{
    return AllocSurfaceImpl(static_cast<Renderer*>(r), width, height, record, flags);
}

void ui::renderer::ReleaseSurface(void* r, int index)
{
    return ReleaseSurfaceImpl(static_cast<Renderer*>(r), index);
}

bool ui::renderer::ResizeSurface(void* r, int index, int width, int height)
{
    return ResizeSurfaceImpl(static_cast<Renderer*>(r), index, width, height);
}

void ui::renderer::RecreateSurface(void* r, int index)
{
    return RecreateSurfaceImpl(static_cast<Renderer*>(r), index);
}

void ui::renderer::RestoreLostSurfaces(void* r)
{
    return RestoreLostSurfacesImpl(static_cast<Renderer*>(r));
}

void ui::renderer::SetActiveSurface(void* r, int index)
{
    return SetActiveSurfaceImpl(static_cast<Renderer*>(r), index);
}

uint32_t ui::renderer::GetSurfacePitch(void* r)
{
    return GetSurfacePitchImpl(static_cast<Renderer*>(r));
}

void ui::renderer::UnlockSurface(void* r)
{
    return UnlockSurfaceImpl(static_cast<Renderer*>(r));
}

void ui::renderer::GetSurfaceOrigin(void* r, int* origin)
{
    return GetSurfaceOriginImpl(static_cast<Renderer*>(r), origin);
}

void ui::renderer::SetSurfaceOrigin(void* r, int index, int x, int y)
{
    return SetSurfaceOriginImpl(static_cast<Renderer*>(r), index, x, y);
}

bool ui::renderer::IsSurfaceLost(void* r, int index)
{
    return IsSurfaceLostImpl(static_cast<Renderer*>(r), index);
}

void ui::renderer::RestoreSurface(void* r, int index)
{
    return RestoreSurfaceImpl(static_cast<Renderer*>(r), index);
}

void ui::renderer::ClearSurface(void* r, int index)
{
    return ClearSurfaceImpl(static_cast<Renderer*>(r), index);
}

void ui::renderer::BlitSurface(void* r, int x, int y, int source)
{
    return BlitSurfaceImpl(static_cast<Renderer*>(r), x, y, source);
}

void ui::renderer::BlitSurfaceKeyed(void* r, int x, int y, int destination, int source)
{
    return BlitSurfaceKeyedImpl(static_cast<Renderer*>(r), x, y, destination, source);
}

void ui::renderer::BlitSurfaceRect(void* r, const int* dstXY, const int* rect, int source)
{
    return BlitSurfaceRectImpl(static_cast<Renderer*>(r), dstXY, rect, source);
}

} // namespace obl::ui
