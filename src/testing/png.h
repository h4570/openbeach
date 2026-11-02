#pragma once

#include <algorithm>
#include <cstdint>

namespace obl::test {

// std::filesystem::path::value_type: the unit tests also build on Linux (tests/unit/linux), where paths are narrow.
#ifdef _WIN32
using PathChar = wchar_t;
#else
using PathChar = char;
#endif

// Writes a 16-bit RGB555 image (the windowed back buffer format) as a 24-bit PNG.
bool WritePngRgb555(const PathChar* path, const uint16_t* pixels, int width, int height, int pitchPixels);

} // namespace obl::test
