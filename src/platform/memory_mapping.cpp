#include "game/ui/memory_mapping.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <vector>
#endif

namespace obl::ui {

#ifdef _WIN32
void* CreateMemoryMapping(uint32_t bytes)
{
    return CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, bytes, nullptr);
}

void CloseMemoryMapping(void* mapping)
{
    CloseHandle(mapping);
}

void* MapMemoryView(void* mapping, MappingAccess access, uint32_t offset, uint32_t size)
{
    return MapViewOfFile(mapping, access == MappingAccess::All ? FILE_MAP_ALL_ACCESS : FILE_MAP_WRITE, 0, offset, size);
}

void UnmapMemoryView(void* mapped)
{
    UnmapViewOfFile(mapped);
}
#else
// Without page-file mappings the cache is one heap block: a view is a pointer into it, so unmapping frees nothing.
struct MemoryMapping {
    std::vector<uint8_t> bytes;
};

void* CreateMemoryMapping(uint32_t bytes)
{
    return new MemoryMapping{std::vector<uint8_t>(bytes)};
}

void CloseMemoryMapping(void* mapping)
{
    delete static_cast<MemoryMapping*>(mapping);
}

void* MapMemoryView(void* mapping, MappingAccess, uint32_t offset, uint32_t size)
{
    std::vector<uint8_t>& bytes = static_cast<MemoryMapping*>(mapping)->bytes;
    if (offset > bytes.size() || size > bytes.size() - offset) return nullptr;
    return bytes.data() + offset;
}

void UnmapMemoryView(void*) {}
#endif

} // namespace obl::ui
