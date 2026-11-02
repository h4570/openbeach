#include "game/ui/renderer_api.h"
// Renderer life cycle on the SDL path, first part (docs/architecture/presentation.md): Present, IsModeSupported and ChangeResolution.
// Only the system-memory display exists (windowed and full screen); there is no DirectDraw full screen (Flip, SetDisplayMode).

#include "renderer_display.h"

#include "game/game/game_accessors.h"
#include "core/config.h"
#include "core/log.h"
#include "game/ui/renderer_ops.h"
#include "game/ui/resolutions.h"
#include "platform/window_placement.h"
#include "platform/sdl_present.h"
#include "core/call_counter.h"

#include <cstring>

namespace obl::ui {
namespace {
using screen::Rect;
using ui::Renderer;
using ui::RendererFade;
using ui::RendererSurface;
using ui::SurfaceAt;

test::CallCounter g_present("RendererPresent");
test::CallCounter g_modeSupported("RendererIsModeSupported");
test::CallCounter g_changeResolution("RendererChangeResolution");

RendererFade& Fade()
{
    return game::GetRendererFade();
}

uint16_t* BackBufferBits(Renderer* r)
{
    return obl::ui::renderer::GetBackBufferBits(r);
}

void Present(Renderer* r)
{
    if (r->m_captureActive) {
        // ui::renderer::CaptureRectToJpeg clears the request after writing m_captureFileName.
        Rect rect{0, 0, r->m_width, r->m_height};
        obl::ui::renderer::CaptureRectToJpeg(r, &rect);
    }
    r->m_activeSurface = 0;
    uint16_t* bits = BackBufferBits(r);
    const int mode = renderer_ops::StepFade(Fade());
    if (mode >= 0) // the fade also runs before the null check
        obl::ui::renderer::ApplyFade(bits, r->m_width, r->m_height, mode);
    if (!bits) return;
    if (config::Get().headless) {
        // No window to present to (docs/architecture/headless/index.md): the frame stays in the back buffer.
        r->m_surfaceLocked = 0;
        return;
    }
    // A failed present drops the frame; the presenter logs why.
    platform::PresentBackBuffer(bits, static_cast<int>(obl::ui::renderer::GetSurfacePitch(r)), r->m_width, r->m_height, r->m_rgb555 != 0);
    r->m_surfaceLocked = 0;
}

// RendererConstruct always takes the windowed branch, so no DirectDraw flip path is left.
void PresentImpl(Renderer* r)
{
    Present(r);
    g_present.Count();
}

bool ModeSupported(const Renderer* r, int width)
{
    const int index = renderer_ops::ModeSupportedIndex(width);
    return index >= 0 && r->m_modeSupported[index] != 0;
}

// Checked before gameplay starts in the saved resolution (800x600 when false) and by the Options list.
bool IsModeSupportedImpl(Renderer* r, int width)
{
    if (ui::IsListedResolutionWidth(width)) return true;
    g_modeSupported.Count();
    return ModeSupported(r, width);
}

// What follows every windowed resolution change.
void FinishWindowedResolution(Renderer* r)
{
    // The display frees and reallocates the work buffer but leaves the pixel-format globals' cached pointer and width dangling: the next
    // frame drew into freed memory (heap corruption, a crash when only the height changed).
    obl::ui::renderer::InitPixelFormatGlobals(r->m_workBuffer, r->m_width, r->m_rgb555);
    if (r->m_window) platform::PlaceGameWindow(r->m_window, r->m_width, r->m_height);
}

// The windowed branch always succeeds. Its two SetWindowPos calls (client size at the screen's top-left corner) are left out: the window
// placement ([Display] Scale, borderless on the monitor) replaces them anyway.
void ChangeResolution(Renderer* r, int width, int height)
{
    r->m_width = width;
    r->m_height = height;
    obl::ui::renderer::ResizeSurface(r, 0, width, height);
    r->m_view[0] = r->m_view[1] = 0;
    r->m_view[2] = width;
    r->m_view[3] = height;
    const uint32_t pixels = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    ::operator delete(r->m_workBuffer);
    r->m_workBuffer = static_cast<uint32_t*>(::operator new(pixels * 4u));
    if (r->m_lightBuffer) {
        ::operator delete(r->m_lightBuffer);
        r->m_lightBuffer = static_cast<uint8_t*>(::operator new(pixels));
        std::memset(r->m_lightBuffer, 0, pixels);
    }
    r->m_lightsDirty = 1;
    r->m_lightsDirtyFrom = 0;
    FinishWindowedResolution(r);
}

bool ChangeResolutionImpl(Renderer* r, int width, int height)
{
    ChangeResolution(r, width, height);
    g_changeResolution.Count();
    return true;
}

} // namespace

void ui::renderer::Present(void* r)
{
    return PresentImpl(static_cast<Renderer*>(r));
}

bool ui::renderer::IsModeSupported(void* r, int width)
{
    return IsModeSupportedImpl(static_cast<Renderer*>(r), width);
}

bool ui::renderer::ChangeResolution(void* r, int width, int height)
{
    return ChangeResolutionImpl(static_cast<Renderer*>(r), width, height);
}

} // namespace obl::ui
