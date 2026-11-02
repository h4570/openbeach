#pragma once

#include <cstdint>

namespace obl::ui {

struct MapCacheSlot {
    uint32_t m_x;
    uint32_t m_y;
    uint16_t m_colour[0x4000];
    uint16_t m_depth[0x4000];
};

struct MapCache {
    MapCache* Construct(const char* path);
    void Close();
    const uint16_t* GetDepthBlock(uint32_t x, uint32_t y);
    const uint16_t* GetColourBlock(uint32_t x, uint32_t y);
    void LoadColumn(int column, void* destination);
    int32_t m_nextSlot;
    MapCacheSlot m_slots[4];
};

} // namespace obl::ui
