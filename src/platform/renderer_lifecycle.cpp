#include "game/ui/renderer_api.h"
// Renderer life cycle on the SDL path, second part (docs/architecture/presentation.md): Construct and Destruct.
// The windowed branch (also the system-memory full screen) is the only one. Display construction uses the native facade;
// windowed_display.cpp decides how the display is made.

#include "platform/renderer_display.h"
#include "game/ui/image.h"
#include "game/map/water_anim.h"

#include "game/game/game_accessors.h"
#include "core/config.h"
#include "core/log.h"
#include "platform/window_placement.h"
#include "core/call_counter.h"

#include <cstdio>
#include <cstring>
#include <iterator>

namespace obl::ui {
namespace {
using screen::Rect;
using ui::Renderer;
using ui::RendererSurface;
using ui::WaterAnim;

test::CallCounter g_construct("RendererConstruct");
test::CallCounter g_destruct("RendererDestruct");
void (*g_destroyedObserver)() = nullptr;

void InitPixelFormatGlobals(const Renderer* r)
{
    obl::ui::renderer::InitPixelFormatGlobals(r->m_workBuffer, r->m_width, r->m_rgb555);
}

void FreeBlock(void* block)
{
    ::operator delete(block);
}

void DestructImage(void* image)
{
    ui::images::Destruct(image);
}

// A record gives its pixel block back.
void ReleaseSurfaceRecord(RendererSurface* record)
{
    if (!record->m_inUse) return;
    ::operator delete(record->m_bits);
    record->m_bits = nullptr;
    record->m_inUse = 0;
    record->m_width = 0;
    record->m_height = 0;
    record->m_pitch = 0;
}

void FreeWaterData(WaterAnim* water)
{
    if (water->m_data) ::operator delete(water->m_data);
}

// `r` is a value-initialized `new Renderer{}` (game::game_api::Create), so the fields it never
// writes are zero.
void Construct(Renderer* r, void* window, int width, int height)
{
    game::SetRenderer(r);
    r->m_windowed = 1;
    r->m_width = width;
    r->m_height = height;
    r->m_window = window;
    r->m_lightsDirty = 1;
    r->m_zoomLevel = 1;
    r->m_zoomScale = 1.0f;
    r->m_activeSurface = -1;
    // False ends the game with ERROR_DIRECTX after the constructor; the rest is still built, as before.
    r->m_displayOk = obl::ui::renderer::CreateDisplay(r, window, true, width, height, 16);
    r->m_view[2] = width;
    r->m_view[3] = height;
    const uint32_t pixels = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    r->m_workBuffer = static_cast<uint32_t*>(::operator new(pixels * 4u));
    InitPixelFormatGlobals(r);
    r->m_lightBuffer = static_cast<uint8_t*>(::operator new(pixels));
    std::memset(r->m_lightBuffer, 0, pixels);
    r->m_lightsEnabled = 1;
    r->m_lightsDirty = 0;
    r->m_renderVisibleObjects = 0;
    void* water = ::operator new(sizeof(WaterAnim));
    r->m_waterAnim = water ? water_anim::ConstructAt(water) : nullptr;
    r->m_bottomTrim = 0x38;
    for (int i = 0; i < static_cast<int>(std::size(r->m_hazeImages)); ++i) {
        char path[96];
        std::snprintf(path, sizeof(path), ".\\Data\\Haze\\wibble_%02d.tif", i);
        r->m_hazeImages[i] = static_cast<ui::RendererImage*>(ui::images::LoadTiff(path));
    }
    r->m_clipRecordsDirty = 0;
}

// The work buffer is freed too.
void Destruct(Renderer* r)
{
    for (ui::RendererImage* image : r->m_hazeImages) {
        if (image) {
            DestructImage(image);
            FreeBlock(image);
        }
    }
    FreeBlock(r->m_workBuffer);
    for (RendererSurface* record : r->m_surfaces) {
        if (record) {
            ReleaseSurfaceRecord(record);
            delete record;
        }
    }
    r->m_surfaces.clear();
    if (r->m_lightBuffer) FreeBlock(r->m_lightBuffer);
    if (WaterAnim* water = r->m_waterAnim) {
        FreeWaterData(water);
        FreeBlock(water);
    }
    r->m_dirtyRects.clear();
    game::SetRenderer(nullptr);
}

} // namespace

// WinMain hardcodes windowed = false; the windowed branch is forced here for every display (the C++ WinMain in
// platform_main.cpp sets g_WindowedMode to match).
Renderer* ConstructRenderer(Renderer* r, void* window, int width, int height, int windowed)
{
    OBL_LOG("RendererConstruct(%dx%d, windowed %d -> 1)", width, height, windowed & 0xff);
    Construct(r, window, width, height);
    g_construct.Count();
    if (window) platform::PlaceGameWindow(window, width, height);
    return r;
}

void DestructRenderer(Renderer* r)
{
    Destruct(r);
    g_destruct.Count();
    if (g_destroyedObserver) g_destroyedObserver();
}

void SetRendererDestroyedObserver(void (*observer)())
{
    g_destroyedObserver = observer;
}

} // namespace obl::ui
