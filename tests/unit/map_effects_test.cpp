

#include "unit_test.h"

#include "game/ui/map_effects.h"

#include <vector>

using namespace obl;
using namespace obl::map_effects;

namespace {

constexpr uint16_t kHalfMask565 = 0x7bef;

uint32_t Pixel(uint16_t depth, uint16_t color)
{
    return (static_cast<uint32_t>(depth) << 16) | color;
}

struct Scene {
    int width, height;
    std::vector<uint32_t> pixels;
    Scene(int w, int h, uint32_t fill) : width(w), height(h), pixels(static_cast<size_t>(w) * h, fill) {}
    SceneBuffer View() { return {pixels.data(), width, height}; }
    uint32_t& At(int x, int y) { return pixels[static_cast<size_t>(y) * width + x]; }
    int CountColor(int y, uint16_t color) const
    {
        int count = 0;
        for (int x = 0; x < width; ++x) count += (pixels[static_cast<size_t>(y) * width + x] & 0xffff) == color;
        return count;
    }
};

map_math::Terrain FlatTerrain()
{
    return {4096, 4096, nullptr, 0.0f};
}

TileHighlight SampleHighlight()
{
    TileHighlight h{};
    h.tileSize = 64;
    h.tile[0] = 2;
    h.tile[1] = 2;
    h.neighbour2[0] = 3;
    h.neighbour2[1] = 3;
    h.neighbour3[0] = 2;
    h.neighbour3[1] = 4;
    h.origin[0] = 64.0f;
    h.origin[1] = 96.0f;
    h.zoomLevel = 1;
    h.zoomScale = 1.0f;
    h.color = 0xf81f;
    return h;
}

} // namespace

OBL_TEST_CASE("map_effects.darken_halves_the_colour_of_pixels_not_above_the_row_depth")
{
    Scene scene(4, 2, 0);
    scene.At(0, 0) = Pixel(0x0010, 0xffff);
    scene.At(1, 0) = Pixel(0x0011, 0xffff);
    scene.At(2, 0) = Pixel(0x0000, 0xffff);
    scene.At(3, 0) = Pixel(0xffff, 0xffff);
    scene.At(0, 1) = Pixel(0x0020, 0xffff);
    scene.At(1, 1) = Pixel(0x0021, 0xffff);
    scene.At(2, 1) = Pixel(0x0000, 0x0001);
    scene.At(3, 1) = Pixel(0x0010, 0x7bef);
    DarkenByDepth(scene.View(), 1, 0, 0, kHalfMask565);
    OBL_CHECK_EQ(scene.At(0, 0), Pixel(0x0010, 0x3df7));
    OBL_CHECK_EQ(scene.At(1, 0), Pixel(0x0011, 0xffff));
    OBL_CHECK_EQ(scene.At(2, 0), Pixel(0x0000, 0x3df7));
    OBL_CHECK_EQ(scene.At(3, 0), Pixel(0xffff, 0xffff));
    OBL_CHECK_EQ(scene.At(0, 1), Pixel(0x0020, 0x3df7));
    OBL_CHECK_EQ(scene.At(1, 1), Pixel(0x0021, 0xffff));
    OBL_CHECK_EQ(scene.At(2, 1), Pixel(0x0000, 0x0000));
    OBL_CHECK_EQ(scene.At(3, 1), Pixel(0x0010, 0x3df7));
}

OBL_TEST_CASE("map_effects.darken_gives_rows_outside_the_map_depth_zero")
{
    Scene scene(2, 3, 0);
    scene.At(0, 0) = Pixel(0, 0xffff);
    scene.At(1, 0) = Pixel(1, 0xffff);
    scene.At(0, 2) = Pixel(16, 0xffff);
    scene.At(1, 2) = Pixel(17, 0xffff);
    DarkenByDepth(scene.View(), -1, 0, 0, kHalfMask565);
    OBL_CHECK_EQ(scene.At(0, 0), Pixel(0, 0x3df7));
    OBL_CHECK_EQ(scene.At(1, 0), Pixel(1, 0xffff));
    OBL_CHECK_EQ(scene.At(0, 2), Pixel(16, 0x3df7));
    OBL_CHECK_EQ(scene.At(1, 2), Pixel(17, 0xffff));

    Scene edge(1, 2, Pixel(0x0100, 0xffff));
    DarkenByDepth(edge.View(), 0xfff, 0, 0, kHalfMask565);
    OBL_CHECK_EQ(edge.At(0, 0), Pixel(0x0100, 0x3df7));
    OBL_CHECK_EQ(edge.At(0, 1), Pixel(0x0100, 0xffff));
}

OBL_TEST_CASE("map_effects.darken_depth_adds_the_slant_and_the_plane_offset")
{
    Scene scene(2, 1, 0);
    scene.At(0, 0) = Pixel(16 + 5 + 256, 0xffff);
    scene.At(1, 0) = Pixel(16 + 5 + 256 + 1, 0xffff);
    DarkenByDepth(scene.View(), 16, 5, 4096, kHalfMask565);
    OBL_CHECK_EQ(scene.At(0, 0), Pixel(277, 0x3df7));
    OBL_CHECK_EQ(scene.At(1, 0), Pixel(278, 0xffff));
}

OBL_TEST_CASE("map_effects.darken_ignores_an_empty_scene")
{
    Scene scene(0, 5, 0xffffffffu);
    DarkenByDepth(scene.View(), 0, 0, 0, kHalfMask565);
    SceneBuffer none{nullptr, 4, 0};
    DarkenByDepth(none, 0, 0, 0, kHalfMask565);
}

OBL_TEST_CASE("map_effects.highlight_draws_the_top_rows_of_the_diamond")
{
    Scene scene(160, 128, Pixel(0, 0));
    DrawTileHighlight(scene.View(), FlatTerrain(), SampleHighlight());

    OBL_CHECK_EQ(scene.CountColor(48, 0xf81f), 1);
    OBL_CHECK_EQ(scene.At(65, 48) & 0xffff, 0xf81fu);
    OBL_CHECK_EQ(scene.CountColor(49, 0xf81f), 2);
    OBL_CHECK_EQ(scene.At(62, 49) & 0xffff, 0xf81fu);
    OBL_CHECK_EQ(scene.At(64, 49) & 0xffff, 0xf81fu);
    OBL_CHECK_EQ(scene.CountColor(50, 0xf81f), 5);
    for (int x = 61; x <= 69; x += 2) OBL_CHECK_EQ(scene.At(x, 50) & 0xffff, 0xf81fu);
    OBL_CHECK_EQ(scene.CountColor(47, 0xf81f), 0);

    OBL_CHECK_EQ(scene.CountColor(97, 0xf81f), 94 - 15);
    OBL_CHECK_EQ(scene.CountColor(113, 0xf81f), 0);

    OBL_CHECK_EQ(scene.At(65, 48) >> 16, 0u);
}

OBL_TEST_CASE("map_effects.highlight_writes_only_where_the_scene_depth_is_not_above_the_row_depth")
{
    Scene scene(160, 128, Pixel(0x0310, 0));
    scene.At(65, 48) = Pixel(0x0311, 0);
    scene.At(62, 49) = Pixel(0x0320, 0);
    scene.At(64, 49) = Pixel(0x0321, 0);
    DrawTileHighlight(scene.View(), FlatTerrain(), SampleHighlight());
    OBL_CHECK_EQ(scene.At(65, 48), Pixel(0x0311, 0));
    OBL_CHECK_EQ(scene.At(62, 49), Pixel(0x0320, 0xf81f));
    OBL_CHECK_EQ(scene.At(64, 49), Pixel(0x0321, 0));

    OBL_CHECK_EQ(scene.CountColor(50, 0xf81f), 5);
}

OBL_TEST_CASE("map_effects.highlight_scales_the_corners_and_the_depth_step_at_zoom_two")
{
    TileHighlight h = SampleHighlight();
    h.zoomLevel = 2;
    h.zoomScale = 0.5f;

    Scene deep(160, 128, Pixel(0x0330, 0));
    DrawTileHighlight(deep.View(), FlatTerrain(), h);
    OBL_CHECK_EQ(deep.CountColor(24, 0xf81f), 0);
    OBL_CHECK_EQ(deep.CountColor(25, 0xf81f), 2);
    OBL_CHECK_EQ(deep.CountColor(23, 0xf81f), 0);

    Scene shallow(160, 128, Pixel(0x0310, 0));
    DrawTileHighlight(shallow.View(), FlatTerrain(), h);
    OBL_CHECK_EQ(shallow.CountColor(24, 0xf81f), 1);
    OBL_CHECK_EQ(shallow.At(33, 24) & 0xffff, 0xf81fu);

    Scene between(160, 128, Pixel(0x0325, 0));
    DrawTileHighlight(between.View(), FlatTerrain(), h);
    OBL_CHECK_EQ(between.CountColor(25, 0xf81f), 2);
}

OBL_TEST_CASE("map_effects.highlight_clips_to_the_scene")
{
    TileHighlight h = SampleHighlight();
    h.cameraX = -5000;
    h.cameraY = -5000;
    Scene scene(32, 32, Pixel(0, 0));
    DrawTileHighlight(scene.View(), FlatTerrain(), h);
    for (uint32_t pixel : scene.pixels) OBL_CHECK_EQ(pixel, Pixel(0, 0));

    h.cameraX = 64;
    h.cameraY = 40;
    Scene edge(32, 80, Pixel(0, 0));
    DrawTileHighlight(edge.View(), FlatTerrain(), h);
    int written = 0;
    for (uint32_t pixel : edge.pixels) written += (pixel & 0xffff) == 0xf81f;
    OBL_CHECK(written > 0);
}

OBL_TEST_CASE("map_effects.sea_range_covers_the_view_in_blocks")
{
    SeaRange range = VisibleSeaBlocks(300, 200, 800, 600, 128, 1.0f);
    OBL_CHECK_EQ(range.firstColumn, 2);
    OBL_CHECK_EQ(range.firstRow, 1);
    OBL_CHECK_EQ(range.endColumn, 9);
    OBL_CHECK_EQ(range.endRow, 7);

    range = VisibleSeaBlocks(300, 200, 800, 600, 128, 0.5f);
    OBL_CHECK_EQ(range.endColumn, 15);
    OBL_CHECK_EQ(range.endRow, 11);
}

OBL_TEST_CASE("map_effects.sea_range_rounds_toward_zero_like_the_exe")
{
    const SeaRange range = VisibleSeaBlocks(-300, -300, 800, 800, 128, 1.0f);
    OBL_CHECK_EQ(range.firstColumn, -2);
    OBL_CHECK_EQ(range.endColumn, 4);
    OBL_CHECK_EQ(VisibleSeaBlocks(-1000, 0, 800, 1, 128, 1.0f).endColumn, 0);
    OBL_CHECK_EQ(VisibleSeaBlocks(0, 0, 10, 10, 0, 1.0f).endColumn, 0);
}

OBL_TEST_CASE("map_effects.block_origin_is_relative_to_the_camera")
{
    const SeaBlockOrigin origin = BlockOrigin(128, 3, 2, 300, 200);
    OBL_CHECK_EQ(origin.x, 84);
    OBL_CHECK_EQ(origin.y, 56);
}
