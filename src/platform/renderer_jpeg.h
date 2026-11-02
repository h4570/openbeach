#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace obl::renderer_jpeg {
std::vector<uint8_t> ToRgb(const uint16_t* bits, int pitchPixels, int width, int height, bool rgb555);
std::vector<uint8_t> Encode(const uint16_t* bits, int pitchPixels, int width, int height, bool rgb555, int quality);
bool Write(const char* path, const uint16_t* bits, int pitchPixels, int width, int height, bool rgb555, int quality);
// Packed top-down BGR pixels, with no row padding.
bool WriteBgr(const char* path, const void* bits, int width, int height, int quality);
std::string NextPhotoName(const std::vector<std::string>& names);
} // namespace obl::renderer_jpeg
