#pragma once

#include <cstdint>

namespace obl::platform {
bool FileWriteAll(const char* path, const void* data, uint32_t size);
// The BGR `dib` of `rect` (left, top, right, bottom) as a JPEG file.
void JpegWrite(const int32_t* rect, const void* dib, const char* file, int quality);
} // namespace obl::platform
