#include <new>

#include "game/ui/image.h"

#include "game/game/game_accessors.h"
#include "game/ui/renderer_types.h"
#include "core/log.h"
#include "core/pack_path.h"
#include "core/image_scale.h"
#include "core/call_counter.h"
#include "core/os.h"
#include "game/game/diagnostics.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace obl::ui::images {
namespace {

using GameImage = ui::RendererImage;

// Call counters (`call-report ui.tga-load`, `ui.image-build-half-size-chain`).
test::CallCounter g_chainCalls("ui.image-build-half-size-chain");
test::CallCounter g_paletteCalls("ui.image-build-palette16");
std::string g_lastFailedImage;

bool ReadFileBytes(const char* path, std::vector<uint8_t>& out)
{
    FILE* fp = os::OpenFile(path, "rb");
    if (!fp) return false;
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    out.resize(size > 0 ? static_cast<size_t>(size) : 0);
    bool ok = size > 0 && std::fread(out.data(), 1, out.size(), fp) == out.size();
    std::fclose(fp);
    return ok;
}

using DecodeFunction = bool (*)(const uint8_t*, size_t, image::Image&);

bool DecodeFile(const char* path, DecodeFunction decode, image::Image& out)
{
    std::vector<uint8_t> bytes;
    return ReadFileBytes(path, bytes) && decode(bytes.data(), bytes.size(), out);
}

void* BuildGameImage(const image::Image& image)
{
    auto* game = new GameImage;
    Construct(game, image.width, image.height, image.bpp, image.hasPalette() ? 1 : 0);
    if (!image.pixels.empty()) std::memcpy(game->m_pixels, image.pixels.data(), image.pixels.size());
    if (image.hasPalette()) std::memcpy(game->m_palette, image.palette.data(), image.palette.size());
    return game;
}

void FreeGameImage(void* object)
{
    if (!object) return;
    Destruct(object);
    delete static_cast<GameImage*>(object);
}

void* Load(DecodeFunction decode, const char* path)
{
    image::Image decoded;
    auto pack = image::PackPath(path);
    if (!pack.empty()) {
        // PackPath answers `packs/base/<logical>`; the selected pack answers it, and the mod layer may shadow it.
        pack = pack_path::Resolve(pack_path::Selected(), pack.lexically_relative("packs/base"));
        std::string error;
        if (image::LoadPng(pack, decoded, error)) return BuildGameImage(decoded);
        game::FatalError("Cannot load %s: %s. Import the game data again", pack.string().c_str(), error.c_str());
        return nullptr;
    }
    if (DecodeFile(path, decode, decoded)) return BuildGameImage(decoded);
    // Some callers probe optional files (scenario height maps), so a miss is not an error here.
    OBL_LOG("image: cannot load %s", path);
    g_lastFailedImage = path;
    return nullptr;
}

void* __cdecl LoadTgaImage(const char* path)
{
    return Load(&image::DecodeTga, path);
}

bool Is555(const void* renderer)
{
    return static_cast<const ui::Renderer*>(renderer)->m_rgb555 != 0;
}

void BuildImageChain(void* renderer, const GameImage* src, GameImage* dst)
{
    auto* from = src->m_pixels16.data();
    auto* to = dst->m_pixels16.data();
    g_chainCalls.Count();
    image::BuildHalfSizeChain(from, src->m_width, src->m_height, to, dst->m_width, dst->m_height, Is555(renderer));
}
void ConvertImagePalette(GameImage* object, char is555)
{
    g_paletteCalls.Count();
    if (!object) {
        game::FatalError(
                "Cannot build palette: no image; last missing file: %s", g_lastFailedImage.empty() ? "(none)" : g_lastFailedImage.c_str());
        return;
    }
    auto* out = static_cast<uint16_t*>(::operator new(512));
    object->m_palette16 = out;
    if (object->m_palette)
        image::BuildPalette16(object->m_palette, is555 != 0, out);
    else
        std::memset(out, 0, 512);
}
GameImage* RandomImage16(uint32_t& rng, int width, int height)
{
    auto next = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    };
    auto* image = new GameImage;
    Construct(image, width, height, 16, 0);
    auto* pixels = image->m_pixels16.data();
    uint32_t mode = next() % 3; // noise, smooth gradient, noise with long runs
    for (int i = 0; i < width * height; ++i) {
        uint32_t noise = next();
        if (mode == 1)
            pixels[i] = static_cast<uint16_t>(i * 37 + (i / width) * 211);
        else if (mode == 2 && (noise & 3))
            pixels[i] = i ? pixels[i - 1] : 0;
        else
            pixels[i] = static_cast<uint16_t>(noise);
    }
    return image;
}

uint32_t Fold(uint64_t hash)
{
    return static_cast<uint32_t>(hash ^ (hash >> 32));
}

void DriveChain(int count)
{
    uint32_t rng = 9001;
    auto next = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return rng >> 8;
    };
    auto owned = std::make_unique<ui::Renderer>(); // only the 555 flag is read
    ui::Renderer* renderer = owned.get();
    uint64_t hash = 1469598103934665603ull;
    for (int i = 0; i < count; ++i) {
        int sw, sh, dw, dh;
        for (;;) {
            sw = 2 + next() % 69;
            sh = 2 + next() % 69;
            uint32_t big = next() % 8;
            dw = 2 + next() % (big ? sw : 80);
            dh = 2 + next() % (big ? sh : 80);
            int cw = sw, ch = sh;
            while (dw <= cw / 2) {
                cw /= 2;
                ch /= 2;
            }
            if (cw >= 2 && ch >= 2) break;
        }
        renderer->m_rgb555 = static_cast<int>(next() & 1);
        GameImage* src = RandomImage16(rng, sw, sh);
        GameImage* dst = RandomImage16(rng, dw, dh);
        BuildImageChain(renderer, src, dst);
        for (size_t b = 0; b < static_cast<size_t>(dw) * dh * 2; ++b) hash = (hash ^ dst->m_pixels[b]) * 1099511628211ull;
        FreeGameImage(src);
        FreeGameImage(dst);
    }
    OBL_LOG("autotest: image-scale-drive chain hash=%u", Fold(hash));
}

void DrivePalette(int count)
{
    uint32_t rng = 31337;
    uint64_t hash = 1469598103934665603ull;
    for (int i = 0; i < count; ++i) {
        auto* image = new GameImage;
        Construct(image, 1, 1, 8, 1);
        for (int b = 0; b < 768; ++b) {
            rng = rng * 1664525u + 1013904223u;
            image->m_palette[b] = static_cast<uint8_t>(rng >> 16);
        }
        ConvertImagePalette(image, static_cast<char>(i & 1));
        auto* bytes = reinterpret_cast<const uint8_t*>(image->m_palette16);
        for (int b = 0; b < 512; ++b) hash = (hash ^ bytes[b]) * 1099511628211ull;
        FreeGameImage(image);
    }
    OBL_LOG("autotest: image-scale-drive palette hash=%u", Fold(hash));
}

} // namespace

void* LoadTiff(const char* path)
{
    return Load(&image::DecodeTiff, path);
}

void* LoadTga(const char* path)
{
    return LoadTgaImage(path);
}

void Free(void* image)
{
    FreeGameImage(image);
}

void BuildPalette16(void* image, bool is555)
{
    ConvertImagePalette(static_cast<GameImage*>(image), is555 ? 1 : 0);
}

image::Image Read(const void* object)
{
    image::Image image;
    if (!object) return image;
    auto* game = static_cast<const GameImage*>(object);
    image.width = game->m_width;
    image.height = game->m_height;
    image.bpp = game->m_bpp;
    size_t bytes = static_cast<size_t>(game->m_width) * static_cast<size_t>(game->m_height) * static_cast<size_t>(game->m_bytesPerPixel);
    if (game->m_pixels && bytes) image.pixels.assign(game->m_pixels, game->m_pixels + bytes);
    if (game->m_palette) image.palette.assign(game->m_palette, game->m_palette + 768);
    return image;
}

bool RegisterDrivers()
{
    g_chainCalls.SetDriver(&DriveChain);
    g_paletteCalls.SetDriver(&DrivePalette);
    return true;
}

} // namespace obl::ui::images

#include "core/rect.h"
#include "game/ui/renderer_ops.h"

#include <cstdint>

namespace obl::ui::images {
namespace {

using screen::Rect;

// ---- the functions ---------------------------------------------------------------------------------------------------------------------

void InitializeImage(ui::RendererImage* image, int width, int height, int bpp, bool hasPalette)
{
    const int bytes = (bpp + static_cast<int>(static_cast<uint32_t>(bpp >> 31) & 7u)) >> 3;
    image->m_bytesPerPixel = bytes;
    image->m_pixels = nullptr;
    image->m_palette = nullptr;
    image->m_width = width;
    image->m_height = height;
    image->m_bpp = bpp;
    image->m_palette16 = nullptr;
    const uint32_t size = static_cast<uint32_t>(bytes) * static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    image->m_pixels16.clear();
    if (bpp == 16) {
        image->m_pixels16.resize(size / sizeof(uint16_t));
        image->m_pixels = reinterpret_cast<uint8_t*>(image->m_pixels16.data());
    } else {
        image->m_pixels = static_cast<uint8_t*>(::operator new(size));
        std::memset(image->m_pixels, 0, size);
    }
    if (hasPalette) {
        auto* palette = static_cast<uint8_t*>(::operator new(0x300));
        image->m_palette = palette;
        std::memset(palette, 0, 0x300);
    }
}

void InitializeImage16(ui::RendererImage* image, int width, int height)
{
    image->m_width = width;
    image->m_height = height;
    image->m_bpp = 0x10;
    image->m_bytesPerPixel = 2;
    image->m_pixels = nullptr;
    image->m_palette = nullptr;
    image->m_palette16 = nullptr;
}

void DestroyImageBuffers(ui::RendererImage* image)
{
    if (image->m_bpp != 16) ::operator delete(image->m_pixels);
    image->m_pixels16.clear();
    image->m_pixels = nullptr;
    if (image->m_palette) ::operator delete(image->m_palette);
    if (image->m_palette16) ::operator delete(image->m_palette16);
}

} // namespace

void* Construct(void* image, int width, int height, int bpp, char hasPalette)
{
    InitializeImage(static_cast<ui::RendererImage*>(image), width, height, bpp, hasPalette != 0);
    return image;
}

void* Construct16(void* image, int width, int height)
{
    InitializeImage16(static_cast<ui::RendererImage*>(image), width, height);
    return image;
}

void Destruct(void* image)
{
    DestroyImageBuffers(static_cast<ui::RendererImage*>(image));
}

// The slot is only read; the image it points to is destroyed and freed, the slot keeps its (now dangling) value.
void ReleaseSlot(void** slot)
{
    if (void* image = *slot) {
        DestroyImageBuffers(static_cast<ui::RendererImage*>(image));
        delete static_cast<ui::RendererImage*>(image);
    }
}

} // namespace obl::ui::images
