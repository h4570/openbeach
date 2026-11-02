#include "core/image_compare.h"

#include <algorithm>
#include <cstdlib>

namespace obl::image {

namespace {

uint8_t Expand5(uint32_t v)
{
    return static_cast<uint8_t>((v << 3) | (v >> 2));
}
uint8_t Expand6(uint32_t v)
{
    return static_cast<uint8_t>((v << 2) | (v >> 4));
}

} // namespace

Rgb8 PixelAt(const ImageView& image, int x, int y)
{
    const auto* row = static_cast<const uint8_t*>(image.pixels) + static_cast<size_t>(y) * image.pitchBytes;
    switch (image.format) {
    case PixelFormat::Rgb555: {
        uint32_t p = reinterpret_cast<const uint16_t*>(row)[x];
        return {Expand5((p >> 10) & 31), Expand5((p >> 5) & 31), Expand5(p & 31)};
    }
    case PixelFormat::Rgb565: {
        uint32_t p = reinterpret_cast<const uint16_t*>(row)[x];
        return {Expand5((p >> 11) & 31), Expand6((p >> 5) & 63), Expand5(p & 31)};
    }
    case PixelFormat::Xrgb8888:
    default: {
        uint32_t p = reinterpret_cast<const uint32_t*>(row)[x];
        return {static_cast<uint8_t>(p >> 16), static_cast<uint8_t>(p >> 8), static_cast<uint8_t>(p)};
    }
    }
}

CompareResult Compare(const ImageView& actual, const ImageView& expected, int tolerance, std::vector<uint32_t>* diff)
{
    CompareResult result;
    if (diff) diff->clear();
    if (actual.width != expected.width || actual.height != expected.height || actual.width <= 0 || actual.height <= 0) return result;

    result.sizeMatch = true;
    result.totalPixels = static_cast<uint64_t>(actual.width) * static_cast<uint64_t>(actual.height);
    if (diff) diff->resize(static_cast<size_t>(result.totalPixels));

    uint64_t errorSum = 0;
    for (int y = 0; y < actual.height; ++y) {
        for (int x = 0; x < actual.width; ++x) {
            Rgb8 a = PixelAt(actual, x, y);
            Rgb8 e = PixelAt(expected, x, y);
            int dr = std::abs(a.r - e.r), dg = std::abs(a.g - e.g), db = std::abs(a.b - e.b);
            int err = std::max({dr, dg, db});
            errorSum += static_cast<uint64_t>(dr + dg + db);
            result.maxChannelError = std::max(result.maxChannelError, err);
            bool differs = err > tolerance;
            if (differs) ++result.differingPixels;
            if (diff) {
                uint32_t out;
                if (differs) {
                    // Small errors must stay visible against the dimmed background, so start at half intensity.
                    out = static_cast<uint32_t>(128 + err * 127 / 255) << 16;
                } else {
                    uint32_t grey = (e.r * 77u + e.g * 150u + e.b * 29u) >> 10; // luma / 4
                    out = (grey << 16) | (grey << 8) | grey;
                }
                (*diff)[static_cast<size_t>(y) * actual.width + x] = out;
            }
        }
    }
    result.meanChannelError = static_cast<double>(errorSum) / (3.0 * static_cast<double>(result.totalPixels));
    return result;
}

} // namespace obl::image
