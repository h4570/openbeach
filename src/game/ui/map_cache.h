#pragma once

#include "game/ui/map_cache_types.h"
#include "formats/map_pack.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace obl::ui::map_cache {

using ui::MapCache;
using ui::MapCacheSlot;

inline constexpr int kBlocksPerSide = 32;
inline constexpr int kSlots = 4;
inline constexpr uint32_t kEmpty = 0xffffffffu;
inline constexpr uint32_t kPlaneWords = 0x4000;
inline constexpr uint32_t kBlockSide = map_pack::kIslandBlockSide;

template<class Env> MapCache* Construct(MapCache& map, const char* path, Env& env)
{
    if (!env.Open(path)) env.CouldNotOpen(path);

    for (MapCacheSlot& slot : map.m_slots) slot.m_x = slot.m_y = kEmpty;
    map.m_nextSlot = 0;
    return &map;
}

template<class Env> void Close(MapCache& map, Env& env)
{
    env.Close();
}

inline void ConvertToRgb555(uint16_t* words, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) words[i] = static_cast<uint16_t>(((words[i] >> 1) & 0x7fe0) + (words[i] & 0x1f));
}

inline void CopyBlock(const map_pack::IslandPlanes& planes, uint32_t x, uint32_t y, uint16_t* colour, uint16_t* depth)
{
    for (uint32_t by = 0; by < kBlockSide; ++by)
        for (uint32_t bx = 0; bx < kBlockSide; ++bx) {
            const size_t source = static_cast<size_t>(y * kBlockSide + by) * map_pack::kIslandSide + x * kBlockSide + bx;
            colour[by * kBlockSide + bx] = planes.colour[source];
            depth[by * kBlockSide + bx] = planes.depth[source];
        }
}

template<class Env> void LoadBlock(MapCache& map, int x, int y, Env& env)
{
    MapCacheSlot& slot = map.m_slots[map.m_nextSlot];
    if (const map_pack::IslandPlanes* planes = env.Planes()) CopyBlock(*planes, x, y, slot.m_colour, slot.m_depth);
    if (env.Rgb555()) ConvertToRgb555(slot.m_colour, kPlaneWords);
    slot.m_x = static_cast<uint32_t>(x);
    slot.m_y = static_cast<uint32_t>(y);
}

inline int FindSlot(const MapCache& map, uint32_t x, uint32_t y)
{
    for (int i = 0; i < kSlots; ++i)
        if (map.m_slots[i].m_x == x && map.m_slots[i].m_y == y) return i;
    return -1;
}

inline int32_t GetNextSlot(int32_t current)
{
    int32_t next = static_cast<int32_t>((static_cast<uint32_t>(current) + 1u) & 0x80000003u);
    if (next < 0) next = static_cast<int32_t>(((static_cast<uint32_t>(next) - 1u) | 0xfffffffcu) + 1u);
    return next;
}

template<bool Depth, class Env> uint16_t* GetBlock(MapCache& map, uint32_t x, uint32_t y, Env& env)
{
    int index = FindSlot(map, x, y);
    if (index < 0) {
        LoadBlock(map, static_cast<int>(x), static_cast<int>(y), env);
        index = map.m_nextSlot;
        map.m_nextSlot = GetNextSlot(index);
    }
    MapCacheSlot& slot = map.m_slots[index];
    return Depth ? slot.m_depth : slot.m_colour;
}

template<class Env> void LoadColumn(MapCache&, int column, uint16_t* destination, Env& env)
{
    const map_pack::IslandPlanes* planes = env.Planes();
    if (planes)
        for (int i = 0; i < kBlocksPerSide; ++i) {
            uint16_t* block = destination + i * 2 * kPlaneWords;
            CopyBlock(*planes, static_cast<uint32_t>(i), static_cast<uint32_t>(column), block, block + kPlaneWords);
        }
    if (env.Rgb555())
        for (int i = 0; i < kBlocksPerSide; ++i) ConvertToRgb555(destination + i * 2 * kPlaneWords, kPlaneWords);
}

} // namespace obl::ui::map_cache

#include "core/char16.h"
namespace obl::ui {
bool SetMapPackDirectory(const Text16& gameDirectory);
}
