#pragma once

#include <cstdint>

// Anonymous shared memory for the map view's block cache, implemented by src/platform/memory_mapping.cpp (Windows: a page-file
// mapping, elsewhere one heap block). Headless runs keep no map cache and never call it.
namespace obl::ui {

enum class MappingAccess { All, Write };

void* CreateMemoryMapping(uint32_t bytes);
void CloseMemoryMapping(void* mapping);
void* MapMemoryView(void* mapping, MappingAccess access, uint32_t offset, uint32_t size);
void UnmapMemoryView(void* mapped);

} // namespace obl::ui
