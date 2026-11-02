#pragma once

#include "core/rect.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

// The renderer records: Renderer (one per game, created with the Game), its surfaces, the screen fade, the water
// animation and the borrowed image views. Only native code reads them.
namespace obl::ui {

struct WaterAnim;
struct RendererSurface;
struct RendererImage;

struct Renderer {
    uint8_t m_windowed;
    int m_width;
    int m_height;
    int m_view[4]; // x, y, width, height of the scene view (doubled when zoomed out)
    uint32_t* m_workBuffer; // w*h words: low 16 bits colour, high 16 bits depth (renderer.md)
    screen::Rect m_screenRect; // client rect in screen coordinates
    void* m_window; // the platform's game window (SDL_Window* on the windowed host), opaque here; null headless
    int m_rgb555; // pixel format: 1 RGB555, 0 RGB565
    uint8_t m_modeSupported[4]; // 640x480, 800x600, 1024x768, 1280x960
    uint8_t m_lightsEnabled;
    uint8_t m_lightsDirty;
    uint8_t m_trackDirtyRects; // set by MapView::DrawObjects while it draws: ui::renderer::AddDirtyRect records the rectangles
    uint8_t m_clipRecordsDirty; // ui::renderer::ClipRect records a rectangle only while this and m_trackDirtyRects-style tracking is on
    int m_lightsDirtyFrom;
    int m_zoomLevel; // 0 zoomed in, 1 normal, 2 zoomed out
    float m_zoomScale; // 2.0, 1.0, 0.5
    uint8_t m_directMode; // draw into the locked back buffer (the 16-bit UI and translucent blitters) instead of m_workBuffer
    uint8_t m_renderVisibleObjects;
    int m_bottomTrim;
    std::vector<RendererSurface*> m_surfaces; // surface 0 is the back buffer; a null slot is free
    int m_activeSurface;
    uint8_t m_surfaceLocked;
    uint16_t m_greenMask; // 0x3e0 for RGB555
    std::deque<screen::Rect> m_dirtyRects; // ui::renderer::AddDirtyRect / PopDirtyRect (renderer_dirty.h)
    uint8_t m_captureActive;
    char m_captureFileName[0x83]; // ui::renderer::CaptureRectToJpeg writes its output name here
    uint8_t* m_lightBuffer; // w*h bytes
    WaterAnim* m_waterAnim;
    RendererImage* m_hazeImages[6]; // images of .\Data\Haze\wibble_00..05.tif, loaded when the renderer is built
    uint8_t m_displayOk; // CreateDisplay result; false ends the game with ERROR_DIRECTX
};

// The screen fade run by ui::renderer::Present (one global record, zeroed at start). Present is the only code that touches
// it and it only ever clears m_active, so the fade never runs.
struct RendererFade {
    uint8_t m_active;
    uint8_t m_reverse; // second half: the level steps back down to 0
    int m_level; // ui::renderer::ApplyFade mode 0..4
    int m_hold; // frames left at full fade (20 when the level reaches 4)
    int m_tick; // frames since the last level step
    uint8_t m_holding; // holding at mode 4 until m_hold reaches 0
};

// One element of Renderer::m_surfaces (ui::renderer::CreateSurface allocates it). Surface 0 is the back buffer.
// Every record keeps its pixels in a plain heap block it owns.
struct RendererSurface {
    uint8_t m_inUse; // 0 = free slot, reused by CreateSurface
    uint16_t* m_bits; // owned heap block of m_pitch * m_height bytes
    int m_width;
    int m_height;
    int m_pitch; // bytes per row, 2 * width on the system-memory path
    int m_originX; // coordinate of the surface's top-left pixel (GetSurfaceOrigin); sprites subtract it
    int m_originY;
    uint8_t m_cleared; // set by ClearSurface, cleared again whenever GetBackBufferBits hands the pixels out
};

// The record of surface `index`; the game never passes an index outside the list.
inline RendererSurface* SurfaceAt(const Renderer& r, int index)
{
    return r.m_surfaces[static_cast<size_t>(index)];
}

// WaterAnim (Renderer::m_waterAnim points at it; ui::renderer::LoadWaterAnim fills it): 15 run-length encoded frames of the sea's sparkle mask,
// decoded one at a time into four 128x128 byte masks.
struct WaterAnim {
    uint8_t* m_data; // the frames' runs, on the game heap
    int m_frameOffset[15]; // byte offset of each frame's runs in m_data
    int m_frameRuns[15]; // number of signed-byte runs of each frame (> 0 sets that many mask bytes to 1, <= 0 clears |n|)
    uint8_t m_mask[0x10000]; // [row & 1][column & 1] quadrants of 0x4000 bytes: row bit at 0x8000, column bit at 0x4000 (when set)
    int m_lastFrame; // frame m_mask holds, -1 before the first update
    uint8_t m_loaded;
    uint8_t m_enabled; // Video.SeaEffects setting
};

// Bitmap storage shared by image loading, scaling and renderer drawing.
struct RendererImage {
    int m_width;
    int m_height;
    int m_bpp;
    int m_bytesPerPixel;
    uint8_t* m_pixels; // Byte view for indexed drawing and image serialization.
    std::vector<uint16_t> m_pixels16; // Owns samples when m_bpp is 16.
    uint8_t* m_palette;
    uint16_t* m_palette16;
};

// The three per-column loading-bar increments (InitStartupTables sets them).
struct RendererLoadingGradient {
    float m_blueStep;
    float m_redStep;
    float m_greenStep;
};

} // namespace obl::ui
