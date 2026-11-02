// Golden-shot comparison metric (src/core/image_compare.h) and the shot PNG round trip.

#include "unit_test.h"

#include "core/image_compare.h"
#include "testing/png.h"
#include "temp_dir.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace obl::image;

namespace {

ImageView View555(const std::vector<uint16_t>& px, int w, int h, int pitchPixels)
{
    return {px.data(), w, h, pitchPixels * 2, PixelFormat::Rgb555};
}

ImageView View8888(const std::vector<uint32_t>& px, int w, int h)
{
    return {px.data(), w, h, w * 4, PixelFormat::Xrgb8888};
}

uint16_t Rgb555(int r, int g, int b)
{
    return static_cast<uint16_t>((r << 10) | (g << 5) | b);
}

} // namespace

OBL_TEST_CASE("image.expand_matches_png_writer")
{
    // Bit replication: 5-bit 0 -> 0, 31 -> 255, 16 -> 132; 6-bit 63 -> 255, 32 -> 130.
    std::vector<uint16_t> p555 = {Rgb555(0, 31, 16)};
    Rgb8 a = PixelAt(View555(p555, 1, 1, 1), 0, 0);
    OBL_CHECK_EQ(int(a.r), 0);
    OBL_CHECK_EQ(int(a.g), 255);
    OBL_CHECK_EQ(int(a.b), 132);

    std::vector<uint16_t> p565 = {static_cast<uint16_t>((31 << 11) | (32 << 5) | 1)};
    Rgb8 b = PixelAt({p565.data(), 1, 1, 2, PixelFormat::Rgb565}, 0, 0);
    OBL_CHECK_EQ(int(b.r), 255);
    OBL_CHECK_EQ(int(b.g), 130);
    OBL_CHECK_EQ(int(b.b), 8);

    std::vector<uint32_t> p8888 = {0xFF123456u}; // the unused byte is ignored
    Rgb8 c = PixelAt(View8888(p8888, 1, 1), 0, 0);
    OBL_CHECK_EQ(int(c.r), 0x12);
    OBL_CHECK_EQ(int(c.g), 0x34);
    OBL_CHECK_EQ(int(c.b), 0x56);
}

OBL_TEST_CASE("image.identical_across_formats_and_pitch")
{
    // 3x2 RGB555 image with 1 pixel of row padding vs. its 8-bit expansion.
    const int w = 3, h = 2, pitch = 4;
    std::vector<uint16_t> src(pitch * h, 0x7FFF); // padding is white, must not be read
    std::vector<uint32_t> expanded;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            src[y * pitch + x] = Rgb555(x * 10, y * 15, 31 - x);
            Rgb8 c = PixelAt(View555(src, w, h, pitch), x, y);
            expanded.push_back((uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b);
        }
    }
    std::vector<uint32_t> diff;
    CompareResult r = Compare(View555(src, w, h, pitch), View8888(expanded, w, h), 0, &diff);
    OBL_CHECK(r.sizeMatch);
    OBL_CHECK_EQ(r.maxChannelError, 0);
    OBL_CHECK_EQ(r.meanChannelError, 0.0);
    OBL_CHECK_EQ(r.differingPixels, 0ull);
    OBL_CHECK_EQ(r.totalPixels, 6ull);
    OBL_CHECK_EQ(r.DifferingPercent(), 0.0);
    OBL_REQUIRE(diff.size() == 6);
    for (uint32_t px : diff) // equal pixels are grey
        OBL_CHECK(((px >> 16) & 0xFF) == (px & 0xFF) && ((px >> 8) & 0xFF) == (px & 0xFF));
}

OBL_TEST_CASE("image.metrics_and_tolerance")
{
    // 2x2 black; actual differs in one pixel by (10, 0, 4) and in another by (0, 255, 0).
    std::vector<uint32_t> expected(4, 0);
    std::vector<uint32_t> actual = {0x000A0004u, 0, 0x0000FF00u, 0};
    std::vector<uint32_t> diff;
    CompareResult r = Compare(View8888(actual, 2, 2), View8888(expected, 2, 2), 0, &diff);
    OBL_CHECK(r.sizeMatch);
    OBL_CHECK_EQ(r.maxChannelError, 255);
    OBL_CHECK(std::fabs(r.meanChannelError - (10.0 + 4.0 + 255.0) / 12.0) < 1e-9);
    OBL_CHECK_EQ(r.differingPixels, 2ull);
    OBL_CHECK_EQ(r.DifferingPercent(), 50.0);
    OBL_REQUIRE(diff.size() == 4);
    OBL_CHECK_EQ(diff[0], uint32_t(128 + 10 * 127 / 255) << 16); // small error stays clearly red
    OBL_CHECK_EQ(diff[2], 0x00FF0000u); // full error = full red
    OBL_CHECK_EQ(diff[1], 0u); // equal black pixel stays black

    // The tolerance only affects the count of differing pixels, not the error statistics.
    CompareResult tol = Compare(View8888(actual, 2, 2), View8888(expected, 2, 2), 10);
    OBL_CHECK_EQ(tol.differingPixels, 1ull);
    OBL_CHECK_EQ(tol.maxChannelError, 255);
    CompareResult all = Compare(View8888(actual, 2, 2), View8888(expected, 2, 2), 255);
    OBL_CHECK_EQ(all.differingPixels, 0ull);
}

OBL_TEST_CASE("image.size_mismatch")
{
    std::vector<uint32_t> a(6, 0), b(6, 0);
    std::vector<uint32_t> diff = {1, 2, 3};
    CompareResult r = Compare(View8888(a, 3, 2), View8888(b, 2, 3), 0, &diff);
    OBL_CHECK(!r.sizeMatch);
    OBL_CHECK_EQ(r.totalPixels, 0ull);
    OBL_CHECK_EQ(r.differingPixels, 0ull);
    OBL_CHECK(diff.empty());
    CompareResult empty = Compare(View8888(a, 0, 0), View8888(b, 0, 0), 0);
    OBL_CHECK(!empty.sizeMatch);
}

OBL_TEST_CASE("image.shot_png_roundtrip")
{
    const int w = 37, h = 300, pitch = 40; // > 64 KiB of scanlines: several stored deflate blocks
    std::vector<uint16_t> src(pitch * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < pitch; ++x) src[y * pitch + x] = static_cast<uint16_t>((x * 7 + y * 131) & 0x7FFF);

    const obl::test::TempPath tmp("obl_unit_shot_roundtrip", ".png");
    const auto& path = tmp.path;
    OBL_REQUIRE(obl::test::WritePngRgb555(path.c_str(), src.data(), w, h, pitch));
    SDL_Surface* loaded = SDL_LoadPNG(path.string().c_str());
    std::filesystem::remove(path);
    OBL_REQUIRE(loaded != nullptr);
    SDL_Surface* xrgb = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_XRGB8888);
    SDL_DestroySurface(loaded);
    OBL_REQUIRE(xrgb != nullptr);
    OBL_CHECK_EQ(xrgb->w, w);
    OBL_CHECK_EQ(xrgb->h, h);
    ImageView decoded{xrgb->pixels, xrgb->w, xrgb->h, xrgb->pitch, PixelFormat::Xrgb8888};
    CompareResult r = Compare(View555(src, w, h, pitch), decoded, 0);
    SDL_DestroySurface(xrgb);
    OBL_CHECK(r.sizeMatch);
    OBL_CHECK_EQ(r.maxChannelError, 0);
    OBL_CHECK_EQ(r.differingPixels, 0ull);
}
