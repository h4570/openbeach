#pragma once

#include "core/os.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

// Writes a whole byte buffer to a file in one call; the platform file writers (file_output.cpp) use it.
namespace obl::file_write {

// "wb", one fwrite of `size` bytes, true only when all of them were written.
inline bool WriteAll(const char* path, const void* data, size_t size)
{
    std::FILE* file = os::OpenFile(path, "wb");
    if (!file) return false;
    const bool complete = std::fwrite(data, 1, size, file) == size;
    std::fclose(file);
    return complete;
}

} // namespace obl::file_write
