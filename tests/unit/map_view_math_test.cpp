

#include "unit_test.h"

#include "game/ui/map_math.h"

#include <climits>
#include <cmath>
#include <vector>

using namespace obl::map_math;

namespace {

Terrain MakeTerrain(const HeightMap* map, float scale, int size = 4096)
{
    return {size, size, map, scale};
}

} // namespace

OBL_TEST_CASE("map_math.height_is_zero_without_a_height_map_or_outside_the_map")
{
    const Terrain flat = MakeTerrain(nullptr, 3.0f);
    OBL_CHECK_EQ(TerrainHeight(flat, 10, 10), 0.0);

    const std::vector<uint8_t> samples(4, 200);
    const HeightMap map{2, 2, samples.data()};
    const Terrain terrain = MakeTerrain(&map, 1.0f);
    OBL_CHECK_EQ(TerrainHeight(terrain, 100, 100), 200.0);
    OBL_CHECK_EQ(TerrainHeight(terrain, -1, 100), 0.0);
    OBL_CHECK_EQ(TerrainHeight(terrain, 100, -1), 0.0);
    OBL_CHECK_EQ(TerrainHeight(terrain, 4096, 100), 0.0);
    OBL_CHECK_EQ(TerrainHeight(terrain, 100, 4096), 0.0);
    OBL_CHECK_EQ(TerrainHeight(terrain, INT_MIN, INT_MAX), 0.0);
}

OBL_TEST_CASE("map_math.height_interpolates_bilinearly_and_clamps_the_last_cell")
{
    const std::vector<uint8_t> samples = {0, 255, 0, 255};
    const HeightMap map{2, 2, samples.data()};
    const Terrain terrain = MakeTerrain(&map, 0.5f, 100);
    OBL_CHECK_EQ(TerrainHeight(terrain, 0, 0), 0.0);
    OBL_CHECK_EQ(TerrainHeight(terrain, 25, 0), 63.75);

    OBL_CHECK_EQ(TerrainHeight(terrain, 50, 0), 0.0);
    OBL_CHECK(std::fabs(TerrainHeight(terrain, 99, 0) - 255.0 * 49.0 / 50.0 * 0.5) < 1e-9);

    const std::vector<uint8_t> rows = {0, 0, 100, 100};
    const HeightMap rowMap{2, 2, rows.data()};
    const Terrain rowTerrain = MakeTerrain(&rowMap, 1.0f, 100);
    OBL_CHECK_EQ(TerrainHeight(rowTerrain, 10, 25), 50.0);
}

OBL_TEST_CASE("map_math.height_of_a_world_point_halves_the_row_and_rejects_negatives")
{
    const std::vector<uint8_t> rows = {0, 0, 100, 100};
    const HeightMap map{2, 2, rows.data()};
    const Terrain terrain = MakeTerrain(&map, 1.0f, 100);
    OBL_CHECK_EQ(TerrainHeightAtWorld(terrain, 10, 50), TerrainHeight(terrain, 10, 25));
    OBL_CHECK_EQ(TerrainHeightAtWorld(terrain, -1, 50), 0.0);

    const std::vector<uint8_t> tilted = {50, 50, 100, 100};
    const HeightMap tiltedMap{2, 2, tilted.data()};
    const Terrain tiltedTerrain = MakeTerrain(&tiltedMap, 1.0f, 100);
    OBL_CHECK_EQ(TerrainHeightAtWorld(tiltedTerrain, 10, -1), 50.0);
    OBL_CHECK_EQ(TerrainHeightAtWorld(tiltedTerrain, 10, -2), 0.0);
}

OBL_TEST_CASE("map_math.depth_adds_the_plane_the_slant_and_sixteen_per_row")
{
    const Terrain flat = MakeTerrain(nullptr, 1.0f);

    const float world[2] = {10.0f, 200.0f};
    OBL_CHECK_EQ(Depth(flat, 0, 4096, world), 100u + 1600u);
    OBL_CHECK_EQ(Depth(flat, 7, 4096, world), 100u + 1600u + 7u);
    OBL_CHECK_EQ(Depth(flat, 0, 0, world), 1600u);
}

OBL_TEST_CASE("map_math.depth_outside_the_rows_keeps_only_the_high_word")
{
    const Terrain flat = MakeTerrain(nullptr, 1.0f);
    const float negative[2] = {10.0f, -2.0f};
    OBL_CHECK_EQ(Depth(flat, 5, 5, negative), 0xffff0000u);
    const float past[2] = {10.0f, 8192.0f};
    OBL_CHECK_EQ(Depth(flat, 5, 5, past), 0u);
    const float far[2] = {10.0f, 2.0f * 70000.0f};
    OBL_CHECK_EQ(Depth(flat, 5, 5, far), 0x10000u);
}

OBL_TEST_CASE("map_math.world_to_int_truncates_and_halves_y")
{
    const float world[3] = {12.9f, 25.9f, 0.0f};
    const MapPoint point = WorldToInt(world);
    OBL_CHECK_EQ(point.x, 12);
    OBL_CHECK_EQ(point.y, 12);
    const float negative[3] = {-12.9f, -25.9f, 0.0f};
    OBL_CHECK_EQ(WorldToInt(negative).x, -12);
    OBL_CHECK_EQ(WorldToInt(negative).y, -12);
}

OBL_TEST_CASE("map_math.tile_to_screen_places_the_corner_and_lifts_it_by_the_height")
{
    const Terrain flat = MakeTerrain(nullptr, 1.0f);

    ScreenPixel odd = TileToScreen(flat, 64, 3, 5);
    OBL_CHECK_EQ(odd.x, 96);
    OBL_CHECK_EQ(odd.y, 160);
    ScreenPixel even = TileToScreen(flat, 64, 2, 5);
    OBL_CHECK_EQ(even.x, 64);
    OBL_CHECK_EQ(even.y, 144);

    const std::vector<uint8_t> samples(4, 100);
    const HeightMap map{2, 2, samples.data()};
    const Terrain hill = MakeTerrain(&map, 1.0f);
    OBL_CHECK_EQ(TileToScreen(hill, 64, 3, 5).y, 160 - 100);
}

OBL_TEST_CASE("map_math.screen_to_world_bisects_until_the_height_is_within_tolerance")
{
    const Terrain flat = MakeTerrain(nullptr, 2.0f);
    float out[3] = {1.0f, 1.0f, 1.0f};
    ScreenToWorldPoint(flat, 100, 200, out);
    OBL_CHECK_EQ(out[0], 100.0f);
    OBL_CHECK_EQ(out[2], 0.0f);

    const double lift = out[1] / 2.0 - 200.0;
    OBL_CHECK(std::fabs(lift) <= 3.0);
    OBL_CHECK(out[1] > 2 * 200.0 - 6.0);

    const Terrain level = MakeTerrain(nullptr, 0.0f);
    ScreenToWorldPoint(level, 7, 9, out);
    OBL_CHECK_EQ(out[1], 18.0f);
}
