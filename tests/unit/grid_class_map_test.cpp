// The tile class map of the terrain grid (src/game/map/grid_class_map.h).

#include "unit_test.h"

#include "game/map/grid_class_map.h"
#include "game/map/grid_state.h"

#include <cstring>
#include <string>
#include <vector>

using namespace obl;
using obl::map::Grid;

namespace {
struct MapEnv {
    std::vector<uint8_t> bytes;
    std::string path;
    void Write(const char* name, const uint32_t* words, uint32_t size)
    {
        path = name;
        bytes.assign(reinterpret_cast<const uint8_t*>(words), reinterpret_cast<const uint8_t*>(words) + size);
    }
    uint32_t Word(size_t index) const
    {
        uint32_t word;
        std::memcpy(&word, bytes.data() + index * 4, 4);
        return word;
    }
};
} // namespace

OBL_TEST_CASE("grid_class_map.tile_class_follows_the_flag_order_and_carries_the_previous_class")
{
    OBL_CHECK_EQ(grid_class_map::TileClass(1, 9), 0x80000001u);
    OBL_CHECK_EQ(grid_class_map::TileClass(3, 9), 0x80000001u); // bit 0 wins over bit 1
    OBL_CHECK_EQ(grid_class_map::TileClass(4, 9), 0x40000004u);
    OBL_CHECK_EQ(grid_class_map::TileClass(8, 9), 0x40000004u);
    OBL_CHECK_EQ(grid_class_map::TileClass(0x10, 9), 0x10u);
    OBL_CHECK_EQ(grid_class_map::TileClass(0x20, 9), 0x20u);
    OBL_CHECK_EQ(grid_class_map::TileClass(0x8040, 9), 0x8040u);
    OBL_CHECK_EQ(grid_class_map::TileClass(0x80, 9), 0x80u);
    OBL_CHECK_EQ(grid_class_map::TileClass(2, 9), 0x80000002u);
    OBL_CHECK_EQ(grid_class_map::TileClass(0x100, 9), 0x100u);
    OBL_CHECK_EQ(grid_class_map::TileClass(0x11, 9), 0x80000001u); // not exactly 0x10, but bit 0 is set
    OBL_CHECK_EQ(grid_class_map::TileClass(0x30, 9), 9u); // matches nothing
    OBL_CHECK_EQ(grid_class_map::TileClass(0x140, 9), 0x100u); // bit 8 outranks the unmatched 0x40
}

OBL_TEST_CASE("grid_class_map.save_walks_columns_and_starts_from_no_class")
{
    Grid grid{};
    grid.m_width = 128;
    grid.m_height = 128;
    grid.m_cells.resize(128 * 128);
    auto& cells = grid.m_cells;
    grid.m_gridFileName = "map.grid";
    cells[0 * 128 + 5].m_flags = 1; // tile (x 5, y 0)
    cells[3 * 128 + 0].m_flags = 4; // tile (x 0, y 3)
    MapEnv env;
    grid_class_map::Save(grid, env);
    OBL_CHECK(env.path == "map.grid");
    OBL_CHECK_EQ(env.bytes.size(), size_t(0x10000));
    OBL_CHECK_EQ(env.Word(0), 0u); // the first tile matches nothing: no class
    OBL_CHECK_EQ(env.Word(3), 0x40000004u); // x 0, y 3
    OBL_CHECK_EQ(env.Word(4), 0x40000004u); // carried
    OBL_CHECK_EQ(env.Word(5 * 128 + 0), 0x80000001u); // x 5, y 0
}
