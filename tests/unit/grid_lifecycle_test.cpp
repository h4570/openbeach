// Construction and destruction of the terrain grid (src/game/map/grid_lifecycle.h).

#include "unit_test.h"

#include "game/map/grid_lifecycle.h"
#include "game/map/grid_state.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace obl;
using namespace obl::grid_lifecycle;
using obl::map::Grid;
using obl::map::GridCell;

namespace {

struct FakeEnv {
    std::vector<uint32_t> file = std::vector<uint32_t>(kFileSide * kFileSide, 0);
    bool heightExists = true;
    std::vector<std::string> heightQueries; // the grid path each HasHeightMap call got
    std::vector<std::string> heightLoads; // the grid path each LoadHeightMap call got
    std::vector<uint8_t> fog = std::vector<uint8_t>(kFogSide * kFogSide, 0);
    std::vector<std::string> log;
    Grid* global = nullptr;
    int freedFiles = 0;

    void SetGlobalGrid(Grid* grid)
    {
        global = grid;
        log.push_back("global");
    }
    bool HasHeightMap(const char* gridPath)
    {
        heightQueries.push_back(gridPath);
        return heightExists;
    }
    void RememberHeightMapName(const char*) { log.push_back("remember"); }
    bool LoadHeightMap(const char* gridPath, obl::map_pack::HeightSamples& out)
    {
        heightLoads.push_back(gridPath);
        log.push_back("height");
        out = {2, 1, {7, 9}};
        return true;
    }
    const uint32_t* ReadGridFile(const char*)
    {
        log.push_back("read");
        return file.data();
    }
    void FreeGridFile(const uint32_t*) { ++freedFiles; }
    // One map pixel is a tile of the grid: 128 pixels = 2 tiles of 64, so the block (column, row) is over tile (2 * column, 2 * row).
    void ScreenToWorld(const int32_t screen[2], float world[3])
    {
        world[0] = static_cast<float>(screen[0]);
        world[1] = static_cast<float>(screen[1]);
        world[2] = 0.0f;
    }
    void WorldToTile(const float world[3], int32_t tile[2])
    {
        tile[0] = static_cast<int32_t>(world[0]) / 64;
        tile[1] = static_cast<int32_t>(world[1]) / 64;
    }
    uint32_t FogStride() { return kFogSide; }
    void MarkFog(uint32_t index) { fog[index] = 1; }
};

} // namespace

OBL_TEST_CASE("grid_lifecycle.construct_stores_the_header_and_tile_coordinates")
{
    FakeEnv env;
    Grid grid{};
    grid.m_boat.m_numPoints = 9;
    grid.m_beachAreas.resize(2);
    ConstructGrid(grid, env, 128, 128, 64, "height.tga", 0.5f, "grid.bin");
    OBL_CHECK(env.global == &grid);
    OBL_CHECK_EQ(grid.m_width, 128u);
    OBL_CHECK_EQ(grid.m_height, 128u);
    OBL_CHECK_EQ(grid.m_tileSize, 64);
    OBL_REQUIRE(grid.m_cells.size() == 128u * 128u);
    OBL_CHECK(grid.m_cells[1].m_objects.empty()); // the cells start empty, no Env call
    OBL_CHECK_EQ(grid.m_cells[1].m_zOffset, 0);
    OBL_CHECK(grid.m_beachAreas.empty() && grid.m_seaAreas.empty());
    OBL_CHECK_EQ(grid.m_boat.m_numPoints, 0);
    OBL_CHECK(std::string(grid.m_gridFileName) == "grid.bin");
    OBL_REQUIRE(grid.HeightMap() != nullptr);
    OBL_CHECK_EQ(grid.HeightMap()->width, 2);
    const std::vector<uint8_t> samples{7, 9};
    OBL_CHECK(grid.HeightMap()->samples == samples);
    OBL_CHECK(grid.m_heightScale == 0.5f);
    // The height map is looked up by the grid's path (the pack keeps it under the map id), not by the height map's name.
    OBL_REQUIRE(env.heightQueries.size() == 1);
    OBL_CHECK(env.heightQueries[0] == "grid.bin");
    OBL_REQUIRE(env.heightLoads.size() == 1);
    OBL_CHECK(env.heightLoads[0] == "grid.bin");
    OBL_CHECK_EQ(env.freedFiles, 1);
    OBL_CHECK_EQ(env.log.front(), std::string("global"));
}

OBL_TEST_CASE("grid_lifecycle.a_missing_height_map_clears_the_pointer_and_the_scale")
{
    FakeEnv env;
    env.heightExists = false;
    Grid grid{};
    grid.m_heightScale = 3.0f;
    ConstructGrid(grid, env, 128, 128, 64, "none.tga", 0.5f, "grid.bin");
    OBL_CHECK(grid.HeightMap() == nullptr);
    OBL_CHECK(grid.m_heightScale == 0.0f);
    for (const std::string& step : env.log) OBL_CHECK(step != "height" && step != "remember");
    OBL_CHECK(env.heightLoads.empty());
}

OBL_TEST_CASE("grid_lifecycle.grid_file_words_island_size_override_and_frame")
{
    FakeEnv env;
    // Word index = outer * 128 + inner and goes to the cell (x = outer, y = inner).
    env.file[5 * 128 + 6] = 1;
    env.file[7 * 128 + 8] = 2 | 0x100 | 0x4;
    env.file[9 * 128 + 10] = 0x8;
    env.file[127 * 128 + 127] = 3;
    Grid grid{};
    ConstructGrid(grid, env, 128, 128, 64, "h.tga", 1.0f, "g.bin");
    OBL_CHECK_EQ(grid.Cell(5, 6).m_flags, 1u);
    OBL_CHECK_EQ(grid.Cell(7, 8).m_flags, 0x100u); // the override bit replaces the rest
    OBL_CHECK_EQ(grid.Cell(9, 10).m_flags, 8u);
    OBL_CHECK_EQ(grid.m_islandSize, 3); // words with a land or water bit: 1, 2|..., 3
    OBL_CHECK_EQ(grid.Cell(127, 127).m_flags, kBorderFlags); // the frame wins over the file
    for (uint32_t u = 0; u < 128; ++u) {
        OBL_CHECK_EQ(grid.Cell(0, u).m_flags, kBorderFlags);
        OBL_CHECK_EQ(grid.Cell(127, u).m_flags, kBorderFlags);
        OBL_CHECK_EQ(grid.Cell(u, 0).m_flags, kBorderFlags);
        OBL_CHECK_EQ(grid.Cell(u, 127).m_flags, kBorderFlags);
    }
    OBL_CHECK_EQ(grid.Cell(1, 1).m_flags, 0u);
}

OBL_TEST_CASE("grid_lifecycle.fog_blocks_cover_five_by_five_cells_with_terrain_bits")
{
    FakeEnv env;
    // Block (column c, row r) looks at the tiles (2c .. 2c + 4, 2r .. 2r + 4): the cell (10, 8) is inside the blocks c = 3..5, r = 2..4 and nowhere else.
    env.file[10 * 128 + 8] = 0x4; // x = 10, y = 8
    env.file[40 * 128 + 40] = 0x1; // not a terrain bit: only the island count
    Grid grid{};
    ConstructGrid(grid, env, 128, 128, 64, "h.tga", 1.0f, "g.bin");
    for (uint32_t row = 0; row < kFogSide; ++row)
        for (uint32_t column = 0; column < kFogSide; ++column)
            OBL_CHECK_EQ(env.fog[row * kFogSide + column], (column >= 3 && column <= 5 && row >= 2 && row <= 4) ? 1 : 0);
}

OBL_TEST_CASE("grid_lifecycle.destruct_empties_the_areas_image_and_cells")
{
    struct DestructEnv {
        std::vector<std::string> log;
        Grid* global = reinterpret_cast<Grid*>(1);
        void SetGlobalGrid(Grid* grid)
        {
            global = grid;
            log.push_back("global");
        }
    } env;
    Grid grid{};
    grid.m_beachAreas.resize(2);
    grid.m_seaAreas.resize(2);
    grid.m_heightMap = obl::map_pack::HeightSamples{1, 1, {5}};
    grid.m_cells.resize(2);
    grid.m_cells[0].m_flags = 10;
    grid.m_cells[1].m_flags = 11;
    DestructGrid(grid, env);
    const std::vector<std::string> expected{"global"};
    OBL_CHECK(env.log == expected);
    OBL_CHECK(grid.m_beachAreas.empty() && grid.m_seaAreas.empty());
    OBL_CHECK(env.global == nullptr);
    OBL_CHECK(grid.HeightMap() == nullptr);
    OBL_CHECK(grid.m_cells.empty());
}
