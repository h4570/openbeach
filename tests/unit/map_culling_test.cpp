

#include "unit_test.h"

#include "game/ui/map_culling.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace obl;
using namespace obl::map_culling;

namespace {

std::vector<map::GridCell> MakeGrid(uint32_t width, uint32_t height)
{
    std::vector<map::GridCell> cells(width * height);
    std::memset(cells.data(), 0, cells.size() * sizeof(map::GridCell));
    return cells;
}

} // namespace

OBL_TEST_CASE("map_culling.object_query_places_the_frame_on_the_screen")
{
    const RectQuery query = ObjectQuery(500, 300, 20, 400, 200, -15, -40, 64, 90);
    OBL_CHECK_EQ(query.x, 85);
    OBL_CHECK_EQ(query.y, 40);
    OBL_CHECK_EQ(query.srcX, 0);
    OBL_CHECK_EQ(query.srcY, 0);
    OBL_CHECK_EQ(query.width, 64u);
    OBL_CHECK_EQ(query.height, 90u);
}

OBL_TEST_CASE("map_culling.object_query_wraps_like_the_exe_registers")
{
    const RectQuery query = ObjectQuery(10, 10, 0, 0, 0, std::numeric_limits<int>::max(), 0, 1, 1);
    OBL_CHECK_EQ(query.x, std::numeric_limits<int>::min() + 9);
    const RectQuery high = ObjectQuery(std::numeric_limits<int>::min(), 0, 1, 0, 0, 0, 0, 1, 1);
    OBL_CHECK_EQ(high.x, std::numeric_limits<int>::min());
    OBL_CHECK_EQ(high.y, -1);
}

OBL_TEST_CASE("map_culling.only_scenery_and_buildings_skip_the_cull")
{
    OBL_CHECK(!DrawObjectsCulls(1));
    OBL_CHECK(!DrawObjectsCulls(2));
    for (int type : {0, 3, 4, 5, 20, 255}) OBL_CHECK(DrawObjectsCulls(static_cast<uint8_t>(type)));
    OBL_CHECK(IsGuestType(3));
    OBL_CHECK(!IsGuestType(2));
}

OBL_TEST_CASE("map_culling.scaled_view_size_truncates_the_division")
{
    OBL_CHECK_EQ(ScaledViewSize(800, 1.0f), 800);
    OBL_CHECK_EQ(ScaledViewSize(800, 0.5f), 1600);
    OBL_CHECK_EQ(ScaledViewSize(800, 2.0f), 400);
    OBL_CHECK_EQ(ScaledViewSize(801, 2.0f), 400);
    OBL_CHECK_EQ(ScaledViewSize(100, 0.0f), 0);
    OBL_CHECK_EQ(ScaledViewSize(0, 0.0f), 0);
}

OBL_TEST_CASE("map_culling.view_corners_double_the_row")
{
    const WorldPoint topLeft = ViewTopLeft(100, 50);
    OBL_CHECK_EQ(topLeft.x, 100.0f);
    OBL_CHECK_EQ(topLeft.y, 100.0f);
    OBL_CHECK_EQ(topLeft.z, 0.0f);
    const WorldPoint bottomRight = ViewBottomRight(100, 50, 800, 500);
    OBL_CHECK_EQ(bottomRight.x, 899.0f);
    OBL_CHECK_EQ(bottomRight.y, 2.0f * 549.0f);
}

OBL_TEST_CASE("map_culling.tile_range_fixes_the_wrapped_corners")
{
    const uint32_t topLeft[2] = {0x80, 3};
    const uint32_t bottomRight[2] = {0, 0};
    const TileRange range = FixTileRange(topLeft, bottomRight);
    OBL_CHECK_EQ(range.x0, 0u);
    OBL_CHECK_EQ(range.y0, 3u);
    OBL_CHECK_EQ(range.x1, 0x80u);
    OBL_CHECK_EQ(range.y1, 0x80u);
    const uint32_t a[2] = {5, 0x80}, b[2] = {9, 7};
    const TileRange plain = FixTileRange(a, b);
    OBL_CHECK_EQ(plain.x0, 5u);
    OBL_CHECK_EQ(plain.y0, 0u);
    OBL_CHECK_EQ(plain.x1, 9u);
    OBL_CHECK_EQ(plain.y1, 7u);
}

OBL_TEST_CASE("map_culling.counts_sea_tiles_in_the_rectangle")
{
    auto cells = MakeGrid(8, 8);
    cells[2 + 3 * 8].m_flags = 0x20000000u;
    cells[3 + 3 * 8].m_flags = 0x40000000u;
    cells[4 + 3 * 8].m_flags = 0x80000000u;
    cells[2 + 4 * 8].m_flags = 0x60000010u;
    OBL_CHECK_EQ(CountSeaTiles(cells.data(), 8, 8, 2, 3, 5, 5), 3);
    OBL_CHECK_EQ(CountSeaTiles(cells.data(), 8, 8, 3, 3, 5, 5), 1);
    OBL_CHECK_EQ(CountSeaTiles(cells.data(), 8, 8, 2, 3, 2, 5), 0);
    OBL_CHECK_EQ(CountSeaTiles(cells.data(), 8, 8, 2, 5, 5, 5), 0);
    OBL_CHECK_EQ(CountSeaTiles(cells.data(), 8, 8, 2, 5, 5, 3), 0);
}

OBL_TEST_CASE("map_culling.count_wraps_over_the_grid_edges")
{
    auto cells = MakeGrid(8, 8);
    cells[1 + 2 * 8].m_flags = kSeaMask;

    OBL_CHECK_EQ(CountSeaTiles(cells.data(), 8, 8, 9, 10, 11, 11), 1);
    OBL_CHECK_EQ(CountSeaTiles(cells.data(), 8, 8, 9, 10, 9, 11), 0);
}

OBL_TEST_CASE("map_culling.sea_share_is_the_count_over_the_area")
{
    const TileRange range{10, 20, 14, 25};
    OBL_CHECK_EQ(SeaShare(0, range), 0.0);
    OBL_CHECK_EQ(SeaShare(-3, range), 0.0);
    OBL_CHECK_EQ(SeaShare(5, range), 5.0 / 20.0);
    OBL_CHECK_EQ(SeaShare(20, range), 1.0);
}
