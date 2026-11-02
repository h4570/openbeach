#pragma once

// PNG reader for the converted pack (docs/architecture/data-formats/assets.md): non-interlaced images in color type 0 (gray, 8 or 16
// bits), 2 (RGB, 8 bits), 3 (palette index, 8 bits) 4 (gray + alpha, 8 bits) or 6 (RGBA, 8 bits), the shapes the converter writes for maps and sprite
// sheets. Palette samples remain indices; the RGB palette is returned separately, so recoloring it does not change terrain samples. Pure logic: no Windows APIs.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace obl::png_read {

// A decoded image: `bytes` holds the samples row by row, `channels` per pixel, `bitDepth / 8` bytes each (16-bit samples big-endian).
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;
    int bitDepth = 0;
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> palette; // RGB entries for indexed images, empty otherwise
    bool indexed = false;
};

// Decodes `bytes` into `image`. False with `error` for a bad signature or chunk, another bit depth, colour type or interlace, a missing
// or misplaced IHDR or IDAT, a broken zlib stream, an unknown filter or an image larger than 16384 pixels a side.
bool Decode(std::span<const uint8_t> bytes, Image& image, std::string& error);

// Decodes `bytes` into `samples`: one byte per pixel (the palette index or the gray level), rows top to bottom. False with `error` when
// `Decode` fails or the image is not 8-bit gray or palette.
bool DecodeSamples(std::span<const uint8_t> bytes, int& width, int& height, std::vector<uint8_t>& samples, std::string& error);

} // namespace obl::png_read
