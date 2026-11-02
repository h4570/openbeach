// obl_imgdiff - compares two PNG images (e.g. a shot from run/captures/ against run/golden/) with the
// same metric as the golden-shot assertions (src/core/image_compare.h, docs/testing/golden-images.md).
//
//   obl_imgdiff <actual.png> <expected.png> [--tol N] [--max-percent P] [--diff out.png]
//
// --tol          per-channel error 0..255 still counted as equal (default 0)
// --max-percent  share of differing pixels that still passes (default 0)
// --diff         write a difference image (red = differing pixel, grey = dimmed expected image)
// Prints one "key=value" line. Exit code: 0 = match, 1 = images differ, 2 = usage or I/O error.

#include "core/image_compare.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels; // 0x00RRGGBB
};

bool LoadPng(const char* path, Image& out)
{
    SDL_Surface* loaded = SDL_LoadPNG(path);
    if (!loaded) {
        std::fprintf(stderr, "cannot load %s: %s\n", path, SDL_GetError());
        return false;
    }
    SDL_Surface* xrgb = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_XRGB8888);
    SDL_DestroySurface(loaded);
    if (!xrgb) {
        std::fprintf(stderr, "cannot convert %s: %s\n", path, SDL_GetError());
        return false;
    }
    out.width = xrgb->w;
    out.height = xrgb->h;
    out.pixels.resize(static_cast<size_t>(out.width) * out.height);
    for (int y = 0; y < out.height; ++y) {
        const auto* row =
                reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(xrgb->pixels) + static_cast<size_t>(y) * xrgb->pitch);
        for (int x = 0; x < out.width; ++x) out.pixels[static_cast<size_t>(y) * out.width + x] = row[x] & 0x00FFFFFFu;
    }
    SDL_DestroySurface(xrgb);
    return true;
}

obl::image::ImageView View(const Image& image)
{
    return {image.pixels.data(), image.width, image.height, image.width * 4, obl::image::PixelFormat::Xrgb8888};
}

bool SaveDiff(const char* path, std::vector<uint32_t>& pixels, int width, int height)
{
    SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_XRGB8888, pixels.data(), width * 4);
    bool ok = surface && SDL_SavePNG(surface, path);
    if (!ok) std::fprintf(stderr, "cannot write %s: %s\n", path, SDL_GetError());
    SDL_DestroySurface(surface);
    return ok;
}

int Usage()
{
    std::fprintf(stderr, "usage: obl_imgdiff <actual.png> <expected.png> [--tol N] [--max-percent P] [--diff out.png]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    const char* paths[2] = {};
    int pathCount = 0;
    int tolerance = 0;
    double maxPercent = 0.0;
    const char* diffPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tol" && i + 1 < argc) {
            tolerance = std::atoi(argv[++i]);
        } else if (arg == "--max-percent" && i + 1 < argc) {
            maxPercent = std::atof(argv[++i]);
        } else if (arg == "--diff" && i + 1 < argc) {
            diffPath = argv[++i];
        } else if (!arg.starts_with("--") && pathCount < 2) {
            paths[pathCount++] = argv[i];
        } else {
            return Usage();
        }
    }
    if (pathCount != 2 || tolerance < 0 || tolerance > 255 || maxPercent < 0.0) return Usage();

    Image actual, expected;
    if (!LoadPng(paths[0], actual) || !LoadPng(paths[1], expected)) return 2;

    std::vector<uint32_t> diff;
    obl::image::CompareResult r = obl::image::Compare(View(actual), View(expected), tolerance, diffPath ? &diff : nullptr);
    if (!r.sizeMatch) {
        std::printf("size_match=0 actual=%dx%d expected=%dx%d\n", actual.width, actual.height, expected.width, expected.height);
        return 1;
    }
    bool pass = r.DifferingPercent() <= maxPercent;
    std::printf("size_match=1 size=%dx%d max_err=%d mean_err=%.4f diff_pixels=%llu diff_percent=%.4f tol=%d %s\n", actual.width,
            actual.height, r.maxChannelError, r.meanChannelError, static_cast<unsigned long long>(r.differingPixels), r.DifferingPercent(),
            tolerance, pass ? "PASS" : "DIFFERS");
    if (diffPath && !SaveDiff(diffPath, diff, actual.width, actual.height)) return 2;
    return pass ? 0 : 1;
}
