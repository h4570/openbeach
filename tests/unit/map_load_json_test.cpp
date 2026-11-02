#include "unit_test.h"
#include "game/map/grid_state.h"
#include "game/world/map_load_json.h"
#include "game/world/save_sections_json.h"

#include <string>

using namespace obl;

namespace {

// A 2 x 2 island with two beach areas and one sea area.
map::Grid SmallGrid()
{
    map::Grid grid;
    grid.m_width = grid.m_height = 2;
    grid.m_cells.resize(4);
    grid.m_beachAreas.resize(2);
    grid.m_seaAreas.resize(1);
    return grid;
}

} // namespace

OBL_TEST_CASE("map_load_json.reads_back_what_the_writer_wrote")
{
    map::Grid grid = SmallGrid();
    grid.m_islandSize = 3;
    grid.m_beachAreas[1].m_shut = true;
    grid.m_cells[0].m_flags = 0xffffffffu;
    grid.m_cells[3].m_flags = 0x41;
    grid.m_cells[2].m_zOffset = 0xffff;
    nlohmann::ordered_json written;
    std::string error;
    OBL_REQUIRE(world_save::WriteMapJson(grid, written, error));

    map::Grid loaded = SmallGrid();
    loaded.m_seaAreas[0].m_shut = true;
    loaded.m_cells[1].m_flags = 7;
    OBL_REQUIRE(world_save::ReadMapJson(nlohmann::json(written), loaded, error));
    OBL_CHECK(error.empty());
    OBL_CHECK(world_save::CheckMapJson(nlohmann::json(written), error));
    OBL_CHECK(loaded.m_islandSize == 3);
    OBL_CHECK(!loaded.m_beachAreas[0].m_shut);
    OBL_CHECK(loaded.m_beachAreas[1].m_shut);
    OBL_CHECK(!loaded.m_seaAreas[0].m_shut);
    for (size_t i = 0; i < 4; ++i) {
        OBL_CHECK(loaded.m_cells[i].m_flags == grid.m_cells[i].m_flags);
        OBL_CHECK(loaded.m_cells[i].m_zOffset == grid.m_cells[i].m_zOffset);
    }
}

OBL_TEST_CASE("map_load_json.missing_island_size_reads_as_zero")
{
    const auto in = nlohmann::json::parse(R"({"cells":[2,2],"areas_open":[true,false,true],"flags":[0,0,0,0],"heights":[0,0,0,0]})");
    map::Grid grid = SmallGrid();
    grid.m_islandSize = 9;
    std::string error;
    OBL_REQUIRE(world_save::ReadMapJson(in, grid, error));
    OBL_CHECK(grid.m_islandSize == 0);
    OBL_CHECK(grid.m_beachAreas[1].m_shut);
}

OBL_TEST_CASE("map_load_json.refuses_a_section_the_grid_cannot_hold")
{
    const char* const bad[] = {
            R"({"cells":[2,2],"flags":[0,0,0,0],"heights":[0,0,0,0]})", // no areas
            R"({"cells":[2,3],"areas_open":[true,true,true],"flags":[0,0,0,0,0,0],"heights":[0,0,0,0,0,0]})",
            R"({"cells":[2,2],"areas_open":[true,true,true],"flags":[0,0,0],"heights":[0,0,0,0]})",
            R"({"cells":[2,2],"areas_open":[true,true,true],"flags":[0,0,0,0],"heights":[0,0,0,65536]})",
            R"({"cells":[2,2],"areas_open":[true,1,true],"flags":[0,0,0,0],"heights":[0,0,0,0]})",
    };
    std::string error;
    for (const char* text : bad) {
        map::Grid grid = SmallGrid();
        grid.m_islandSize = 5;
        OBL_CHECK(!world_save::ReadMapJson(nlohmann::json::parse(text), grid, error));
        OBL_CHECK(!error.empty());
        OBL_CHECK(grid.m_islandSize == 5);
    }
    // Valid alone, but not for this island: a larger grid or another area count fails only the read.
    for (const char* text : {R"({"cells":[3,3],"areas_open":[true,true,true],"flags":[0,0,0,0,0,0,0,0,0],"heights":[0,0,0,0,0,0,0,0,0]})",
                 R"({"cells":[2,2],"areas_open":[true,true],"flags":[0,0,0,0],"heights":[0,0,0,0]})"}) {
        const auto in = nlohmann::json::parse(text);
        map::Grid grid = SmallGrid();
        OBL_CHECK(world_save::CheckMapJson(in, error));
        OBL_CHECK(!world_save::ReadMapJson(in, grid, error));
        OBL_CHECK(error.rfind("map.", 0) == 0);
    }
}
