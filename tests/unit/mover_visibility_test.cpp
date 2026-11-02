#include "unit_test.h"
#include "game/objects/mover_visibility.h"
#include "game/objects/object_base.h"

#include <memory>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace obl;
namespace {
using object::Mover;

struct FakeEnv {
    object::Base building{};
    bool hasBuilding = true;
    int screen[2] = {0, 0};
    uint32_t depth = 0;
    uint32_t tile[2] = {0, 0};
    int zoom = 1, viewWidth = 100, viewHeight = 50, stride = 100;
    uint32_t gridWidth = 8, gridHeight = 4;
    std::map<uint32_t, uint16_t> zOffsets;
    std::map<int, uint32_t> buffer;
    std::vector<std::string> calls;
    int lastBufferIndex = -1;
    uint32_t lastCell = 0xffffffffu;

    const object::Base* Building(int)
    {
        calls.push_back("building");
        return hasBuilding ? &building : nullptr;
    }
    void WorldToScreen(const float*, int out[2])
    {
        calls.push_back("screen");
        out[0] = screen[0];
        out[1] = screen[1];
    }
    uint32_t Depth(const float*)
    {
        calls.push_back("depth");
        return depth;
    }
    void TilePosition(const Mover&, uint32_t out[2])
    {
        calls.push_back("tile");
        out[0] = tile[0];
        out[1] = tile[1];
    }
    int ZoomLevel() { return zoom; }
    int ViewWidth() { return viewWidth; }
    int ViewHeight() { return viewHeight; }
    int BufferStride() { return stride; }
    uint32_t BufferWord(int index)
    {
        lastBufferIndex = index;
        const auto it = buffer.find(index);
        return it == buffer.end() ? 0 : it->second;
    }
    uint32_t GridWidth() { return gridWidth; }
    uint32_t GridHeight() { return gridHeight; }
    uint16_t ZOffset(uint32_t cell)
    {
        lastCell = cell;
        const auto it = zOffsets.find(cell);
        return it == zOffsets.end() ? 0 : it->second;
    }
};

// Game objects are not copyable: the fixture lives on the heap.
std::unique_ptr<Mover> ObjectAt(float y)
{
    auto object = std::make_unique<Mover>();
    object->m_WorldXYZ[1] = y;
    return object;
}
} // namespace

OBL_TEST_CASE("moving_visibility.unrecognized_building_is_not_visible")
{
    FakeEnv env;
    env.hasBuilding = false;
    const auto fixture = ObjectAt(5);
    const Mover& object = *fixture;
    OBL_CHECK(!moving::IsVisibleAgainstBuilding(object, 7, env));
    OBL_CHECK_EQ(env.calls.size(), size_t(1));
}

OBL_TEST_CASE("moving_visibility.object_not_behind_the_building_needs_no_screen_lookup")
{
    FakeEnv env;
    env.building.m_WorldXYZ[1] = 10;
    OBL_CHECK(moving::IsVisibleAgainstBuilding(*ObjectAt(10), 1, env));
    OBL_CHECK(moving::IsVisibleAgainstBuilding(*ObjectAt(11), 1, env));
    OBL_CHECK_EQ(env.calls.size(), size_t(2));
}

OBL_TEST_CASE("moving_visibility.nan_goes_on_to_the_depth_test")
{
    FakeEnv env;
    env.building.m_WorldXYZ[1] = 10;
    env.buffer[0] = 0xffff0000u; // nothing can beat this depth
    OBL_CHECK(!moving::IsVisibleAgainstBuilding(*ObjectAt(std::numeric_limits<float>::quiet_NaN()), 1, env));
    env.building.m_WorldXYZ[1] = std::numeric_limits<float>::quiet_NaN();
    OBL_CHECK(!moving::IsVisibleAgainstBuilding(*ObjectAt(20), 1, env));
}

OBL_TEST_CASE("moving_visibility.depth_test_adds_cell_offset_and_bias")
{
    FakeEnv env;
    env.building.m_WorldXYZ[1] = 10;
    env.screen[0] = 3;
    env.screen[1] = 2;
    env.depth = 0x1000;
    env.tile[0] = 1;
    env.tile[1] = 1;
    env.zOffsets[1 * 8 + 1] = 0x20;
    // The object's depth is 0x1000 + 0x20 + 0x30 = 0x1050.
    env.buffer[2 * 100 + 3] = 0x1050u << 16;
    OBL_CHECK(moving::IsVisibleAgainstBuilding(*ObjectAt(5), 1, env));
    OBL_CHECK_EQ(env.lastBufferIndex, 203);
    env.buffer[2 * 100 + 3] = 0x1051u << 16;
    OBL_CHECK(!moving::IsVisibleAgainstBuilding(*ObjectAt(5), 1, env));
    env.buffer[2 * 100 + 3] = 0x1051u << 16 | 0xffffu; // the colour half does not count
    OBL_CHECK(!moving::IsVisibleAgainstBuilding(*ObjectAt(5), 1, env));
}

OBL_TEST_CASE("moving_visibility.depth_sum_wraps_at_sixteen_bits")
{
    FakeEnv env;
    env.building.m_WorldXYZ[1] = 10;
    env.depth = 0x1ffffu; // only the low word is the depth
    env.zOffsets[0] = 0x100;
    env.buffer[0] = 0x0100u << 16; // 0xffff + 0x100 + 0x30 wraps to 0x12f
    OBL_CHECK(moving::IsVisibleAgainstBuilding(*ObjectAt(5), 1, env));
    env.buffer[0] = 0x0130u << 16;
    OBL_CHECK(!moving::IsVisibleAgainstBuilding(*ObjectAt(5), 1, env));
}

OBL_TEST_CASE("moving_visibility.tile_wraps_around_the_grid")
{
    FakeEnv env;
    env.building.m_WorldXYZ[1] = 10;
    env.tile[0] = 8 + 3; // column 3 of 8
    env.tile[1] = 4 + 2; // row 2 of 4
    moving::IsVisibleAgainstBuilding(*ObjectAt(5), 1, env);
    OBL_CHECK_EQ(env.lastCell, uint32_t(2 * 8 + 3));
}

OBL_TEST_CASE("moving_visibility.screen_point_is_halved_when_zoomed_out_and_clamped")
{
    const auto a = moving::ClampToView(-5, -9, 2, 100, 50);
    OBL_CHECK_EQ(a.x, 0);
    OBL_CHECK_EQ(a.y, 0);
    const auto b = moving::ClampToView(-3, 7, 2, 100, 50); // -3 / 2 truncates to -1, then 0
    OBL_CHECK_EQ(b.x, 0);
    OBL_CHECK_EQ(b.y, 3);
    const auto c = moving::ClampToView(500, 500, 1, 100, 50);
    OBL_CHECK_EQ(c.x, 99);
    OBL_CHECK_EQ(c.y, 49);
    const auto d = moving::ClampToView(100, 50, 0, 100, 50);
    OBL_CHECK_EQ(d.x, 99);
    OBL_CHECK_EQ(d.y, 49);
    const auto e = moving::ClampToView(10, 20, 2, 100, 50);
    OBL_CHECK_EQ(e.x, 5);
    OBL_CHECK_EQ(e.y, 10);
}
