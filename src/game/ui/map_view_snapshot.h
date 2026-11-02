#include "game/ui/map_cache.h"
#pragma once

#include "game/game/game_accessors.h"
#include "game/ui/map_view.h"
#include "game/ui/map_blocks.h"
#include "game/ui/map_view_blocks.h"

#include <cstdint>

// Shared by the step 12 writers of the mapped background blocks (the background restorers of map_view_restore.cpp and the building bakers of
// sprite_instance_bake.cpp): the game's side of the block Env of src/game/ui/map_restore.h.
namespace obl::ui::map_snapshot {

using ui::MapView;

class GameEnv {
public:
    explicit GameEnv(MapView& view) : m_view(view) {}

    int BlocksPerRow() const { return m_view.m_blocksPerRow; }
    int BlocksPerColumn() const { return m_view.m_blocksPerColumn; }
    int BlockSize() const { return m_view.m_blockSize; }
    bool Loaded(int index) const { return m_view.m_blocks[index].m_loaded != 0; }
    void Map(uint32_t x, uint32_t y) { (m_view).LoadBlock(static_cast<int>(x), static_cast<int>(y)); }
    uint32_t* View(uint32_t x, uint32_t y)
    {
        return m_view.m_blocks[map_blocks::BlockIndex(m_view.m_blocksPerRow, static_cast<int>(x), static_cast<int>(y))].m_pixels;
    }
    const uint16_t* Depth(uint32_t x, uint32_t y) { return static_cast<ui::MapCache*>(m_view.m_mapCache)->GetDepthBlock(x, y); }
    const uint16_t* Colour(uint32_t x, uint32_t y) { return static_cast<ui::MapCache*>(m_view.m_mapCache)->GetColourBlock(x, y); }

private:
    MapView& m_view;
};

// Deterministic driver randomness (the scene fixtures of sprite_instance_bake.cpp).
struct Rng {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int Range(int low, int high) { return low + static_cast<int>(Next() % static_cast<uint32_t>(high - low + 1)); }
    bool Chance(int percent) { return static_cast<int>(Next() % 100) < percent; }
};

} // namespace obl::ui::map_snapshot
