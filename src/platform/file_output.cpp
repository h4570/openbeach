// Small file writers of the game (src/game/ui/file_output.h), answered by src/platform/file_write.h.

#include "game/ui/file_output.h"

#include "platform/file_write.h"
#include "platform/renderer_jpeg.h"

#include <cstdint>

namespace obl::platform {

void JpegWrite(const int32_t* rect, const void* dib, const char* file, int quality)
{
    renderer_jpeg::WriteBgr(file, dib, rect[2] - rect[0], rect[3] - rect[1], quality);
}

bool FileWriteAll(const char* path, const void* data, uint32_t size)
{
    return file_write::WriteAll(path, data, size);
}

} // namespace obl::platform
