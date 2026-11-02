#include "game/ui/renderer_api.h"
#include "windowed_display.h"

#include "core/config.h"
#include "core/log.h"
#include "platform/renderer_jpeg.h"
#include "platform/renderer_display.h"
#include "game/ui/file_output.h"
#include "game/ui/photo_file.h"
#include "game/game/game_accessors.h"
#include "platform/sdl_present.h"

#include <windows.h>

#include <cstring>
#include <fstream>
#include <filesystem>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STB_IMAGE_STATIC
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "../../../third_party/stb/stb_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace obl::test {
namespace {

bool g_contractRequested;

void OnRendererDestroyed()
{
    if (g_contractRequested) {
        const bool ok = !game::GetRenderer();
        OBL_LOG("autotest: %s display-destroyed", ok ? "PASS" : "FAILED");
    }
}

void CheckContract()
{
    ui::Renderer* renderer = game::GetRenderer();
    if (!renderer || !renderer->m_windowed) {
        OBL_LOG("autotest: FAILED display-contract needs windowed renderer");
        return;
    }
    const ui::RendererSurface* back = renderer->m_surfaces.empty() ? nullptr : renderer->m_surfaces[0];
    int width = renderer->m_width, height = renderer->m_height;
    bool modes = true;
    for (uint8_t supported : renderer->m_modeSupported) modes &= supported == 1;
    bool surface = back && back->m_inUse == 1 && back->m_bits && back->m_width == width && back->m_height == height &&
            back->m_pitch == width * 2 && !back->m_originX && !back->m_originY;
    bool format = renderer->m_rgb555 == 1 && renderer->m_greenMask == 0x3e0;
    bool window = platform::SdlGameWindow(renderer->m_window) != nullptr; // the renderer presents to the SDL game window
    bool buffers = renderer->m_workBuffer && renderer->m_lightBuffer;
    OBL_LOG("autotest: %s display-contract modes=%d surface=%d format=%d window=%d buffers=%d",
            modes && surface && format && window && buffers ? "PASS" : "FAILED", modes, surface, format, window, buffers);
    g_contractRequested = true;
}

// dgVoodoo replaces these system DLLs by sitting next to BeachLife.exe; the default run/ must not have them, so every
// loaded one has to come from outside the game directory (System32/SysWOW64).
void CheckGraphicsModules()
{
    wchar_t gameDir[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, gameDir, MAX_PATH);
    while (n && gameDir[n - 1] != L'\\') --n;
    gameDir[n] = 0;
    int loaded = 0, inGameDir = 0;
    for (const wchar_t* name : {L"ddraw.dll", L"d3d8.dll", L"d3d9.dll", L"d3dimm.dll"}) {
        HMODULE module = GetModuleHandleW(name);
        if (!module) continue;
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(module, path, MAX_PATH);
        bool local = n && _wcsnicmp(path, gameDir, n) == 0 && !wcschr(path + n, L'\\');
        ++loaded;
        inGameDir += local;
        OBL_LOG("graphics module %ls: %ls%s", name, path, local ? " (game directory)" : "");
    }
    OBL_LOG("autotest: graphics-modules loaded=%d game-dir=%d", loaded, inGameDir);
}

bool JpegDecoded(const char* path, int width, int height, bool colorBands = false)
{
    int w = 0, h = 0, channels = 0;
    auto* pixels = stbi_load(path, &w, &h, &channels, 3);
    bool ok = pixels && w == width && h == height && channels == 3;
    if (ok && colorBands) {
        const auto* top = pixels + (8 * w + 8) * 3;
        const auto* bottom = pixels + (40 * w + 8) * 3;
        ok = top[0] > 245 && top[1] < 10 && top[2] < 10 && bottom[0] < 10 && bottom[1] < 10 && bottom[2] > 245;
    }
    stbi_image_free(pixels);
    return ok;
}

bool JpegMatches(const char* path, const ui::RendererSurface& surface, const RECT& rect, bool rgb555)
{
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    auto rgb = renderer_jpeg::ToRgb(surface.m_bits + rect.top * surface.m_width + rect.left, surface.m_width, width, height, rgb555);
    int w = 0, h = 0, channels = 0;
    auto* pixels = stbi_load(path, &w, &h, &channels, 3);
    bool ok = pixels && w == width && h == height;
    if (ok) {
        uint64_t error = 0;
        for (size_t i = 0; i < rgb.size(); ++i) error += std::abs(static_cast<int>(pixels[i]) - rgb[i]);
        // JPEG is lossy: check the mean error, with exact color-band checks separately.
        ok = error < rgb.size() * 25;
    }
    stbi_image_free(pixels);
    return ok;
}

} // namespace

bool RegisterDisplayTrace()
{
    ui::SetRendererDestroyedObserver(&OnRendererDestroyed);
    return true;
}

bool ExecuteDisplayCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "display-contract") {
        CheckContract();
        return true;
    }
    if (name == "graphics-modules") {
        CheckGraphicsModules();
        return true;
    }
    if (name != "display-jpeg") return false;
    ui::Renderer* renderer = game::GetRenderer();
    if (!renderer || args.size() != 1 || args[0].find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) {
        OBL_LOG("autotest: FAILED display-jpeg needs renderer and a simple output name");
        return true;
    }
    std::string rectPath = "captures/" + args[0] + "_rect.jpg";
    std::string surfacePath = "captures/" + args[0] + "_surface.jpg";
    DeleteFileA(rectPath.c_str());
    DeleteFileA(surfacePath.c_str());
    // CaptureRect writes through its inline filename buffer.
    char saved[sizeof(renderer->m_captureFileName)];
    char* path = renderer->m_captureFileName;
    std::memcpy(saved, path, sizeof(saved));
    RECT rect{13, 17, 333, 257};
    strcpy_s(path, sizeof(saved), rectPath.c_str());
    renderer->m_captureActive = 1;
    ui::renderer::CaptureRectToJpeg(renderer, &rect);
    bool state = renderer->m_activeSurface == 0 && !renderer->m_surfaceLocked && !renderer->m_captureActive;
    std::memcpy(path, saved, sizeof(saved));
    ui::renderer::CaptureSurfaceToJpeg(renderer, 0, surfacePath.c_str());
    const auto* back = ui::SurfaceAt(*renderer, 0);
    const RECT full{0, 0, renderer->m_width, renderer->m_height};
    bool ok = state && JpegDecoded(rectPath.c_str(), 320, 240) && JpegDecoded(surfacePath.c_str(), renderer->m_width, renderer->m_height) &&
            JpegMatches(rectPath.c_str(), *back, rect, renderer->m_rgb555 != 0) &&
            JpegMatches(surfacePath.c_str(), *back, full, renderer->m_rgb555 != 0);
    const int index = obl::ui::renderer::CreateSurface(renderer, 35, 49, 0);
    auto* surface = ui::SurfaceAt(*renderer, index);
    for (int y = 0; y < 49; ++y)
        for (int x = 0; x < 35; ++x) surface->m_bits[y * 35 + x] = y < 24 ? 0x7c00 : 0x001f;
    const std::string bands = "captures/" + args[0] + "_bands.jpg";
    DeleteFileA(bands.c_str());
    ui::renderer::CaptureSurfaceToJpeg(renderer, index, bands.c_str());
    ok &= JpegDecoded(bands.c_str(), 35, 49, true) && renderer->m_activeSurface == index && !renderer->m_surfaceLocked;
    // Exercise platform::JpegWrite independently of the renderer's 16-bit encoder.
    std::vector<uint8_t> bgr(35 * 49 * 3);
    for (int y = 0; y < 49; ++y)
        for (int x = 0; x < 35; ++x) bgr[(y * 35 + x) * 3 + (y < 24 ? 2 : 0)] = 255;
    const int32_t bgrRect[]{7, 11, 42, 60};
    const auto bgrCapture = "captures/" + args[0] + "_bgr.jpg";
    DeleteFileA(bgrCapture.c_str());
    platform::JpegWrite(bgrRect, bgr.data(), bgrCapture.c_str(), 95);
    ok &= JpegDecoded(bgrCapture.c_str(), 35, 49, true);
    std::filesystem::create_directory("photos");
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator("photos")) names.push_back(entry.path().filename().string());
    const auto photo = "photos/" + renderer_jpeg::NextPhotoName(names);
    if (std::filesystem::exists(photo))
        ok = false;
    else {
        ui::PhotoAlbumSavePhotoFile(index);
        ok &= JpegDecoded(photo.c_str(), 35, 49, true);
        DeleteFileA(photo.c_str());
    }
    obl::ui::renderer::ReleaseSurface(renderer, index);
    renderer->m_activeSurface = 0;
    OBL_LOG("autotest: %s display-jpeg %s %s decoded crop/surface/bands/photo/BGR", ok ? "PASS" : "FAILED", rectPath.c_str(),
            surfacePath.c_str());
    return true;
}

} // namespace obl::test
