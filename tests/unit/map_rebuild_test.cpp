

#include "unit_test.h"

#include "game/ui/map_rebuild.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace obl;
using obl::object::Building;
using screen::Rect;
using tables::BuildingDef;
using ui::MapView;

namespace {

struct Fixture {
    MapView view{};
    std::vector<std::unique_ptr<obl::object::Building>> buildings;
    std::vector<std::unique_ptr<BuildingDef>> definitions;
    std::set<const obl::object::Building*> demolishing, building;
    std::vector<std::string> log;

    Fixture()
    {
        view.m_blocksPerRow = 32;
        view.m_blocksPerColumn = 32;
        view.m_blockSize = 128;
    }

    std::vector<int32_t> Contents() const { return {view.m_burnQueue.begin(), view.m_burnQueue.end()}; }

    obl::object::Building* AddBuilding(Rect screen, int gridW, int gridH, float x, float y, float z)
    {
        buildings.push_back(std::make_unique<obl::object::Building>());
        definitions.push_back(std::make_unique<BuildingDef>());
        obl::object::Building* object = buildings.back().get();
        definitions.back()->m_GridWidth = gridW;
        definitions.back()->m_GridHeight = gridH;
        object->m_definition = definitions.back().get();
        object->m_rScrRect = screen;
        object->m_WorldXYZ[0] = x;
        object->m_WorldXYZ[1] = y;
        object->m_WorldXYZ[2] = z;
        object->m_id = static_cast<uint32_t>(buildings.size());
        return object;
    }

    void LoadBlockPair(int x, int y) { log.push_back("pair " + std::to_string(x) + " " + std::to_string(y)); }
    void RebuildPathTiles(Rect& rect)
    {
        log.push_back("path " + std::to_string(rect.left) + " " + std::to_string(rect.top) + " " + std::to_string(rect.right) + " " +
                std::to_string(rect.bottom));
    }
    uint32_t NumBuildings() { return static_cast<uint32_t>(buildings.size()); }
    obl::object::Building* Building(uint32_t i) { return buildings[i].get(); }
    const Rect* BuildingRect(obl::object::Building& b) { return &b.m_rScrRect; }

    bool Intersect(const Rect& a, const Rect& b, Rect& out)
    {
        if (a.right < b.left || b.right < a.left || a.bottom < b.top || b.bottom < a.top) return false;
        out = {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
        return true;
    }
    bool HasDemolishJob(obl::object::Building& b) { return demolishing.count(&b) != 0; }
    bool HasBuildJob(obl::object::Building& b) { return building.count(&b) != 0; }
    void BakeBuilding(obl::object::Building& b, const Rect& clip, const float pos[3], int gridW, int gridH)
    {
        char text[160];
        std::snprintf(text, sizeof(text), "bake %u clip %d %d %d %d pos %g %g %g grid %d %d", b.m_id, clip.left, clip.top, clip.right,
                clip.bottom, pos[0], pos[1], pos[2], gridW, gridH);
        log.push_back(text);
    }
};

} // namespace

OBL_TEST_CASE("map_rebuild.queue_pushes_the_rectangle_row_by_row")
{
    Fixture f;
    map_rebuild::QueueBlockRebuild(f, f.view, 3, 2, 3, 2);
    OBL_CHECK(f.Contents() == (std::vector<int32_t>{67, 68, 69, 99, 100, 101}));
    OBL_CHECK_EQ(map_rebuild::QueueSize(f.view), 6);
}

OBL_TEST_CASE("map_rebuild.an_empty_rectangle_queues_nothing")
{
    Fixture f;
    map_rebuild::QueueBlockRebuild(f, f.view, 3, 2, 0, 5);
    map_rebuild::QueueBlockRebuild(f, f.view, 3, 2, 4, 0);
    map_rebuild::QueueBlockRebuild(f, f.view, 3, 2, -2, 3);
    map_rebuild::QueueBlockRebuild(f, f.view, 3, 2, 3, -1);
    OBL_CHECK_EQ(map_rebuild::QueueSize(f.view), 0);
}

OBL_TEST_CASE("map_rebuild.the_rectangle_is_clipped_to_the_block_grid")
{
    Fixture f;
    map_rebuild::QueueBlockRebuild(f, f.view, -1, 30, 3, 4);
    OBL_CHECK(f.Contents() == (std::vector<int32_t>{30 * 32 + 0, 30 * 32 + 1, 31 * 32 + 0, 31 * 32 + 1}));
    Fixture g;
    map_rebuild::QueueBlockRebuild(g, g.view, 174991816, 175014728, 1457863583, 1862656542);
    map_rebuild::QueueBlockRebuild(g, g.view, 40, 0, 5, 5);
    map_rebuild::QueueBlockRebuild(g, g.view, -10, -10, 5, 5);
    OBL_CHECK_EQ(map_rebuild::QueueSize(g.view), 0);
}

OBL_TEST_CASE("map_rebuild.the_row_stride_is_the_blocks_per_row")
{
    Fixture f;
    f.view.m_blocksPerRow = 10;
    map_rebuild::QueueBlockRebuild(f, f.view, 1, 2, 2, 2);
    OBL_CHECK(f.Contents() == (std::vector<int32_t>{21, 22, 31, 32}));
}

OBL_TEST_CASE("map_rebuild.a_long_queue_drains_in_order")
{
    Fixture f;
    map_rebuild::QueueBlockRebuild(f, f.view, 0, 0, 8, 8);
    OBL_CHECK_EQ(map_rebuild::QueueSize(f.view), 64);

    f.view.m_lastUnBurnTime = 9.0f;
    map_rebuild::ProcessRebuildQueue(f, f.view, 200.0f);
    OBL_CHECK_EQ(map_rebuild::QueueSize(f.view), 0);
    int pairs = 0;
    int32_t previous = -1;
    for (const std::string& line : f.log)
        if (line.rfind("pair ", 0) == 0) {
            int x, y;
            OBL_CHECK_EQ(std::sscanf(line.c_str(), "pair %d %d", &x, &y), 2);
            OBL_CHECK(y * 32 + x > previous);
            previous = y * 32 + x;
            ++pairs;
        }
    OBL_CHECK_EQ(pairs, 64);
}

OBL_TEST_CASE("map_rebuild.size_follows_the_served_blocks")
{
    Fixture f;
    map_rebuild::QueueBlockRebuild(f, f.view, 0, 0, 7, 7);
    f.view.m_lastUnBurnTime = 0.0f;
    map_rebuild::ProcessRebuildQueue(f, f.view, 100.0f);
    OBL_CHECK_EQ(map_rebuild::QueueSize(f.view), 49);
    map_rebuild::ProcessRebuildQueue(f, f.view, 135.0f);
    OBL_CHECK_EQ(map_rebuild::QueueSize(f.view), 49 - 35);
}

OBL_TEST_CASE("map_rebuild.a_fresh_clock_restarts_one_period_before_the_time_and_serves_nothing_yet")
{
    Fixture f;
    map_rebuild::QueueBlockRebuild(f, f.view, 0, 0, 3, 1);
    map_rebuild::ProcessRebuildQueue(f, f.view, 10.0f);
    OBL_CHECK_EQ(f.view.m_lastUnBurnTime, 9.0f);
    OBL_CHECK_EQ(map_rebuild::QueueSize(f.view), 3);
    map_rebuild::ProcessRebuildQueue(f, f.view, 10.5f);
    OBL_CHECK_EQ(map_rebuild::QueueSize(f.view), 2);
    OBL_CHECK_EQ(f.view.m_lastUnBurnTime, 10.0f);
    map_rebuild::ProcessRebuildQueue(f, f.view, 13.0f);
    OBL_CHECK_EQ(map_rebuild::QueueSize(f.view), 0);
    OBL_CHECK_EQ(f.view.m_lastUnBurnTime, 12.0f);
}

OBL_TEST_CASE("map_rebuild.the_clock_runs_on_with_an_empty_queue")
{
    Fixture f;
    f.view.m_lastUnBurnTime = 5.0f;
    map_rebuild::ProcessRebuildQueue(f, f.view, 20.0f);
    OBL_CHECK_EQ(f.view.m_lastUnBurnTime, 19.0f);
    OBL_CHECK(f.log.empty());
}

OBL_TEST_CASE("map_rebuild.a_clock_that_is_not_ahead_by_a_whole_period_does_nothing")
{
    Fixture f;
    map_rebuild::QueueBlockRebuild(f, f.view, 0, 0, 2, 1);
    f.view.m_lastUnBurnTime = 9.5f;
    map_rebuild::ProcessRebuildQueue(f, f.view, 10.5f);
    OBL_CHECK_EQ(f.view.m_lastUnBurnTime, 9.5f);
    OBL_CHECK_EQ(map_rebuild::QueueSize(f.view), 2);
    OBL_CHECK(f.log.empty());
}

OBL_TEST_CASE("map_rebuild.a_block_loads_its_pair_rebuilds_the_path_tiles_and_bakes_the_overlapping_buildings")
{
    Fixture f;

    obl::object::Building* inside = f.AddBuilding({300, 140, 340, 200}, 4, 3, 1.5f, 2.5f, 3.5f);
    f.AddBuilding({0, 0, 100, 100}, 2, 2, 0, 0, 0);
    obl::object::Building* demolished = f.AddBuilding({260, 130, 300, 170}, 1, 1, 0, 0, 0);
    obl::object::Building* underConstruction = f.AddBuilding({260, 130, 300, 170}, 1, 1, 0, 0, 0);
    obl::object::Building* edge = f.AddBuilding({384, 256, 500, 400}, 5, 6, 7, 8, 9);
    obl::object::Building* straddling = f.AddBuilding({200, 100, 300, 140}, 2, 3, -1, -2, -3);
    f.demolishing.insert(demolished);
    f.building.insert(underConstruction);
    (void)inside;
    (void)edge;
    (void)straddling;

    map_rebuild::QueueBlockRebuild(f, f.view, 2, 1, 1, 1);
    f.view.m_lastUnBurnTime = 1.0f;
    map_rebuild::ProcessRebuildQueue(f, f.view, 3.0f);
    OBL_CHECK_EQ(f.view.m_lastUnBurnTime, 2.0f);
    OBL_CHECK_EQ(f.log.size(), size_t(5));
    OBL_CHECK_EQ(f.log[0], std::string("pair 2 1"));
    OBL_CHECK_EQ(f.log[1], std::string("path 256 128 384 256"));
    OBL_CHECK_EQ(f.log[2], std::string("bake 1 clip 300 140 340 200 pos 1.5 2.5 3.5 grid 4 3"));
    OBL_CHECK_EQ(f.log[3], std::string("bake 5 clip 384 256 384 256 pos 7 8 9 grid 5 6"));
    OBL_CHECK_EQ(f.log[4], std::string("bake 6 clip 256 128 300 140 pos -1 -2 -3 grid 2 3"));
}

OBL_TEST_CASE("map_rebuild.the_front_block_index_splits_into_column_and_row")
{
    Fixture f;
    f.view.m_blocksPerRow = 20;
    f.view.m_blockSize = 64;
    map_rebuild::QueueBlockRebuild(f, f.view, 19, 3, 1, 1);
    f.view.m_lastUnBurnTime = 0.6f;
    map_rebuild::ProcessRebuildQueue(f, f.view, 5.0f);
    OBL_CHECK_EQ(f.log[0], std::string("pair 19 3"));
    OBL_CHECK_EQ(f.log[1], std::string("path 1216 192 1280 256"));
}
