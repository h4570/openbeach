#include "game/ui/renderer_api.h"
#include "game/ui/photo_file.h"

#include "game/game/game_accessors.h"
#include "core/image_scale.h"
#include "platform/renderer_jpeg.h"
#include "core/log.h"

#include <cstring>
#include <vector>
#include <filesystem>

namespace obl::ui {
namespace {
using screen::Rect;
using ui::Renderer;
using ui::RendererSurface;
using ui::SurfaceAt;

void SaveJpeg(Renderer* r, int surface, const Rect& rect, const char* path, int quality)
{
    r->m_activeSurface = surface;
    const uint16_t* bits = obl::ui::renderer::GetBackBufferBits(r);
    const auto* s = SurfaceAt(*r, surface);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    // System-memory surfaces are packed: the row stride is the surface width.
    if (bits && width > 0 && height > 0 && rect.left >= 0 && rect.top >= 0 && rect.right <= s->m_width && rect.bottom <= s->m_height) {
        bits += static_cast<size_t>(rect.top) * s->m_width + rect.left;
        if (!renderer_jpeg::Write(path, bits, s->m_width, width, height, r->m_rgb555 != 0, quality)) OBL_LOG("JPEG write failed: %s", path);
    }
    obl::ui::renderer::UnlockSurface(r);
}

void CaptureRectToJpegImpl(Renderer* r, const Rect* rect)
{
    SaveJpeg(r, 0, *rect, r->m_captureFileName, 100);
    r->m_captureActive = 0;
}

void CaptureRectToJpegAltImpl(Renderer* r, int surface, const char* path)
{
    const auto* s = SurfaceAt(*r, surface);
    SaveJpeg(r, surface, {0, 0, s->m_width, s->m_height}, path, 80);
}

void SavePhotoFile(int surface)
{
    std::vector<std::string> names;
    std::error_code error;
    std::filesystem::directory_iterator it("photos", error), end;
    while (!error && it != end) {
        names.push_back(it->path().filename().string());
        it.increment(error);
    }
    const std::string path = "photos/" + renderer_jpeg::NextPhotoName(names);
    obl::ui::renderer::CaptureSurfaceToJpeg(game::GetRenderer(), surface, path.c_str());
}

void Downscale(Renderer* r, int source, int destination)
{
    auto* s = SurfaceAt(*r, source);
    auto* d = SurfaceAt(*r, destination);

    image::BuildHalfSizeChain(s->m_bits, s->m_width, s->m_height, d->m_bits, d->m_width, d->m_height, r->m_rgb555 != 0);
}

int Capture(Renderer* r, const Rect* rect)
{
    const int width = rect->right - rect->left, height = rect->bottom - rect->top;
    const int index = obl::ui::renderer::CreateSurface(r, width, height, 0);
    obl::ui::renderer::ClearSurface(r, index);
    r->m_activeSurface = 0;
    const uint16_t* source = obl::ui::renderer::GetBackBufferBits(r);
    obl::ui::renderer::UnlockSurface(r);
    r->m_activeSurface = index;
    uint16_t* destination = obl::ui::renderer::GetBackBufferBits(r);
    if (destination) {
        // Capture uses tightly packed rows, irrespective of the surface pitch or origin.
        for (int y = 0; y < height; ++y)
            std::memcpy(destination + y * width, source + (rect->top + y) * r->m_width + rect->left,
                    static_cast<size_t>(width) * sizeof(uint16_t));
    }
    obl::ui::renderer::UnlockSurface(r);
    return index;
}

void DownscaleSurfaceImpl(Renderer* r, int source, int destination)
{
    Downscale(r, source, destination);
}

int CaptureRectImpl(Renderer* r, const Rect* rect)
{
    return Capture(r, rect);
}

} // namespace

void ui::renderer::DownscaleSurface(void* r, int source, int destination)
{
    return DownscaleSurfaceImpl(static_cast<Renderer*>(r), source, destination);
}

void ui::renderer::CaptureRectToJpeg(void* r, const void* rect)
{
    return CaptureRectToJpegImpl(static_cast<Renderer*>(r), static_cast<const Rect*>(rect));
}

void ui::renderer::CaptureSurfaceToJpeg(void* r, int surface, const char* path)
{
    return CaptureRectToJpegAltImpl(static_cast<Renderer*>(r), surface, path);
}

int ui::renderer::CaptureRect(void* r, const void* rect)
{
    return CaptureRectImpl(static_cast<Renderer*>(r), static_cast<const Rect*>(rect));
}

void PhotoAlbumSavePhotoFile(int surface)
{
    SavePhotoFile(surface);
}
} // namespace obl::ui
