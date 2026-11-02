#pragma once

// Pixel comparison for golden screenshots (docs/testing/golden-images.md). Pure code: used by the unit tests,
// the obl_imgdiff tool and (later) in-game shot assertions, so it must not depend on SDL or the game.

#include <cstdint>
#include <vector>

namespace obl::image {

enum class PixelFormat {
    Rgb555, // the game's 16 bpp back buffer (windowed/system display)
    Rgb565, // 16 bpp RGB565 (the renderer's other back-buffer format)
    Xrgb8888, // 0x00RRGGBB per uint32_t, e.g. a decoded PNG
};

struct ImageView {
    const void* pixels = nullptr;
    int width = 0;
    int height = 0;
    int pitchBytes = 0;
    PixelFormat format = PixelFormat::Xrgb8888;
};

struct Rgb8 {
    uint8_t r, g, b;
};

// Expands to 8 bits per channel exactly like the shot PNG writer (bit replication), so a 16 bpp buffer
// and the PNG saved from it compare as identical.
Rgb8 PixelAt(const ImageView& image, int x, int y);

struct CompareResult {
    bool sizeMatch = false;
    int maxChannelError = 0; // 0..255 over all pixels and channels
    double meanChannelError = 0.0; // mean absolute error over all pixels and the 3 channels
    uint64_t differingPixels = 0; // pixels with any channel error above the tolerance
    uint64_t totalPixels = 0;

    double DifferingPercent() const
    {
        return totalPixels ? 100.0 * static_cast<double>(differingPixels) / static_cast<double>(totalPixels) : 0.0;
    }
};

// Compares two images of any formats. `tolerance` is the per-channel error (0..255) still counted as equal.
// When `diff` is given it receives a width*height 0x00RRGGBB image: equal pixels as a dimmed grey copy of
// `expected`, differing pixels red with brightness growing with the error. Size mismatch: sizeMatch=false,
// everything else zero and `diff` empty.
CompareResult Compare(const ImageView& actual, const ImageView& expected, int tolerance, std::vector<uint32_t>* diff = nullptr);

} // namespace obl::image
