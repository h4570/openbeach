#include "core/file_io.h"
#include "core/os.h"

#include <cstdio>
#include <cstring>

namespace obl::file_io {
namespace {

struct File {
    std::FILE* handle;
    explicit File(const char* path) : handle(os::OpenFile(path, "rb")) {}
    ~File()
    {
        if (handle) std::fclose(handle);
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

} // namespace

uint32_t FileSize(const char* path)
{
    File file(path);
    if (!file.handle || std::fseek(file.handle, 0, SEEK_END) != 0) return 0;
#if defined(_WIN32)
    long long end = _ftelli64(file.handle);
#else
    long long end = ftello(file.handle);
#endif
    return end < 0 ? 0u : static_cast<uint32_t>(end);
}

void* ReadAll(const char* path, Allocate allocate, bool invert)
{
    uint32_t size = FileSize(path);
    if (size == 0) return nullptr;
    File file(path);
    if (!file.handle) return nullptr;
    uint8_t* buffer = static_cast<uint8_t*>(allocate(size_t{size} + 1));
    if (!buffer) return nullptr;
    (void)!std::fread(buffer, 1, size, file.handle);
    if (invert) {
        for (uint32_t i = 0; i < size; ++i) buffer[i] = static_cast<uint8_t>(~buffer[i]);
    }
    buffer[size] = kTerminator;
    return buffer;
}

bool ReadInto(const char* path, void* destination, size_t count)
{
    File file(path);
    if (!file.handle) return false;
    (void)!std::fread(destination, 1, count, file.handle);
    return true;
}

uint32_t ReadScalar(unsigned size, void* stream, uint32_t streamValue, FRead read)
{
    uint32_t slot = streamValue;
    read(&slot, size, 1, stream); // little-endian host: the first byte read is the lowest
    switch (size) {
    case 1: return slot & 0xffu;
    case 2: return slot & 0xffffu;
    default: return slot;
    }
}

} // namespace obl::file_io
