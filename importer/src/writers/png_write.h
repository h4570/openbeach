#pragma once

// 8-bit PNG encoding for the converter's images (docs/architecture/data-formats/assets.md, "Images and pixels").

#include <cstdint>
#include <span>
#include <vector>

namespace obl::convert {

// `channels`: 1 gray, 2 gray + alpha, 3 RGB, 4 RGBA; `pixels` holds width * height * channels bytes, rows top down.
// Lossless and deterministic: the same pixels always give the same bytes.
std::vector<uint8_t> EncodePng(int width, int height, int channels, std::span<const uint8_t> pixels);

// Map planes: indexed 8-bit pixels with an RGB palette, or 16-bit grayscale samples (host integers).
std::vector<uint8_t> EncodeIndexedPng(int width, int height, std::span<const uint8_t> pixels, std::span<const uint8_t> palette);
std::vector<uint8_t> EncodeGray16Png(int width, int height, std::span<const uint16_t> pixels);

} // namespace obl::convert
