
#include "unit_test.h"

#include "platform/renderer_dirty.h"
#include "game/ui/renderer_ops.h"

#include <vector>

using namespace obl;
using namespace obl::renderer_ops;

namespace {

struct Vec {
    std::vector<Rect> rects;
    size_t size() const { return rects.size(); }
    Rect& at(size_t i) { return rects[i]; }
    void swap_remove(size_t i)
    {
        rects[i] = rects.back();
        rects.pop_back();
    }
    void push_back(const Rect& r) { rects.push_back(r); }
};

constexpr blit::Masks kMasks{0x7bde, 0x739c}; // half / quarter mask of RGB555 (the values the game writes for a 555 display)
constexpr blit::WaterColors kColors{0x1ce7, 0x3def, 0x5ef7};

} // namespace

OBL_TEST_CASE("renderer: PackColor keeps 5/5/5 or 5/6/5 bits and lets values above 255 overflow their channel")
{
    OBL_CHECK_EQ(PackColor(true, 255, 255, 255), 0x7fff);
    OBL_CHECK_EQ(PackColor(false, 255, 255, 255), 0xffff);
    OBL_CHECK_EQ(PackColor(true, 0xf8, 0, 0), 0x7c00);
    OBL_CHECK_EQ(PackColor(false, 0xf8, 0, 0), 0xf800);
    OBL_CHECK_EQ(PackColor(true, 0, 0xf8, 0), 0x03e0);
    OBL_CHECK_EQ(PackColor(false, 0, 0xfc, 0), 0x07e0);
    OBL_CHECK_EQ(PackColor(true, 0, 0, 0xff), 0x001f);
    OBL_CHECK_EQ(PackColor(false, 0, 0, 0x08), 0x0001);
    OBL_CHECK_EQ(PackColor(false, 0x100, 0, 0), 0x0000); // 565 masks red with 0xfff8: bit 8 shifts out of the word
    OBL_CHECK_EQ(PackColor(true, 0x100, 0, 0), 0x0000);
}

OBL_TEST_CASE("renderer: ZoomScale follows the zoom level and ignores others")
{
    float scale = 7;
    OBL_CHECK(ZoomScale(0, scale));
    OBL_CHECK_EQ(scale, 2.0f);
    OBL_CHECK(ZoomScale(1, scale));
    OBL_CHECK_EQ(scale, 1.0f);
    OBL_CHECK(ZoomScale(2, scale));
    OBL_CHECK_EQ(scale, 0.5f);
    scale = 7;
    OBL_CHECK(!ZoomScale(3, scale));
    OBL_CHECK_EQ(scale, 7.0f);
}

OBL_TEST_CASE("renderer: ClipRect cuts a sprite to the view at zoom 1 and reports the dirty rectangle")
{
    ClipView view;
    view.x = 10;
    view.y = 20;
    view.width = 100;
    view.height = 50;
    view.recordDirty = true;
    int x = 0, y = 25, left = 0, top = 0, right = 30, bottom = 40; // starts 10 pixels left of the view
    ClipResult r = ClipRect(view, x, y, left, top, right, bottom);
    OBL_CHECK(r.visible);
    OBL_CHECK_EQ(x, 10);
    OBL_CHECK_EQ(left, 10);
    OBL_CHECK_EQ(right, 30);
    OBL_CHECK(r.addDirty);
    OBL_CHECK_EQ(r.dirty.left, 10);
    OBL_CHECK_EQ(r.dirty.top, 25);
    OBL_CHECK_EQ(r.dirty.right, 30);
    OBL_CHECK_EQ(r.dirty.bottom, 65);

    x = 90, y = 60, left = 0, top = 0, right = 40, bottom = 40; // leaves the view on the right and bottom
    r = ClipRect(view, x, y, left, top, right, bottom);
    OBL_CHECK(r.visible);
    OBL_CHECK_EQ(right, 20); // the view ends at 110
    OBL_CHECK_EQ(bottom, 10); // and at 70
    OBL_CHECK_EQ(r.dirty.right, 110);
    OBL_CHECK_EQ(r.dirty.bottom, 70);

    x = 111, y = 30, left = 0, top = 0, right = 4, bottom = 4;
    OBL_CHECK(!ClipRect(view, x, y, left, top, right, bottom).visible);
    x = 0, y = 30, left = 0, top = 0, right = 9, bottom = 4; // ends at x 9 < 10
    OBL_CHECK(!ClipRect(view, x, y, left, top, right, bottom).visible);
    x = 20, y = 15, left = 0, top = 0, right = 4, bottom = 4; // bottom edge 19 < 20
    OBL_CHECK(!ClipRect(view, x, y, left, top, right, bottom).visible);
    view.recordDirty = false;
    x = 20, y = 30, left = 0, top = 0, right = 4, bottom = 4;
    OBL_CHECK(!ClipRect(view, x, y, left, top, right, bottom).addDirty);
}

OBL_TEST_CASE("renderer: ClipRect scales the far edge with the zoom unless drawing directly")
{
    ClipView view;
    view.width = 100;
    view.height = 40;
    int x = 150, y = 10, left = 0, top = 0, right = 100, bottom = 10;
    view.zoomLevel = 1;
    OBL_CHECK(!ClipRect(view, x, y, left, top, right, bottom).visible); // 150 > 100
    view.zoomLevel = 2; // zoomed out: the view is 200 wide in destination pixels
    OBL_CHECK(ClipRect(view, x, y, left, top, right, bottom).visible);
    OBL_CHECK_EQ(right, 50);
    view.zoomLevel = 0; // zoomed in: 50 wide
    x = 20, y = 10, left = 0, top = 0, right = 100, bottom = 100;
    OBL_CHECK(ClipRect(view, x, y, left, top, right, bottom).visible);
    OBL_CHECK_EQ(right, 30);
    OBL_CHECK_EQ(bottom, 10); // 40 / 2 = 20 rows in view, 10 already used
    view.directMode = true;
    x = 20, y = 10, left = 0, top = 0, right = 100, bottom = 100;
    OBL_CHECK(ClipRect(view, x, y, left, top, right, bottom).visible);
    OBL_CHECK_EQ(right, 80); // unscaled
}

OBL_TEST_CASE("renderer: FillRect clip and fills")
{
    const FillSpan s = ClipFill(10, 6, {-3, -2, 20, 4});
    OBL_CHECK_EQ(s.x, 0);
    OBL_CHECK_EQ(s.y, 0);
    OBL_CHECK_EQ(s.width, 10);
    OBL_CHECK_EQ(s.height, 4); // the rows above 0 are cut: bottom 4 stays
    OBL_CHECK_EQ(ClipFill(10, 6, {8, 5, 4, 9}).width, -4); // empty rectangles stay empty
    std::vector<uint32_t> scene(60, 0xaaaa0000u);
    FillScene(scene.data(), 10, ClipFill(10, 6, {8, 4, 14, 9}), 0x1234);
    OBL_CHECK_EQ(scene[4 * 10 + 8], 0xaaaa1234u);
    OBL_CHECK_EQ(scene[5 * 10 + 9], 0xaaaa1234u);
    OBL_CHECK_EQ(scene[4 * 10 + 7], 0xaaaa0000u);
    OBL_CHECK_EQ(scene[3 * 10 + 8], 0xaaaa0000u);
    std::vector<uint16_t> back(30, 0);
    FillBackBuffer(back.data() + 5, 10, {0, 0, 3, 2}, 7);
    OBL_CHECK_EQ(back[5], 7);
    OBL_CHECK_EQ(back[7], 7);
    OBL_CHECK_EQ(back[8], 0);
    OBL_CHECK_EQ(back[15], 7);
    OBL_CHECK_EQ(back[25], 0);
}

OBL_TEST_CASE("renderer: ApplyFade modes")
{
    std::vector<uint16_t> p = {0x7fff, 0x0001, 0x1234, 0x7fff};
    const std::vector<uint16_t> snapshot = p;
    ApplyFade(p.data(), 2, 2, 0, kMasks, kColors);
    OBL_CHECK(p == snapshot);
    ApplyFade(p.data(), 2, 2, 5, kMasks, kColors);
    OBL_CHECK(p == snapshot);
    ApplyFade(p.data(), 2, 0, 4, kMasks, kColors); // no rows
    ApplyFade(p.data(), 0, 2, 4, kMasks, kColors); // no columns
    OBL_CHECK(p == snapshot);
    ApplyFade(p.data(), 2, 2, 4, kMasks, kColors);
    for (uint16_t v : p) OBL_CHECK_EQ(v, 0xffff);
    p = snapshot;
    ApplyFade(p.data(), 4, 1, 2, kMasks, kColors);
    OBL_CHECK_EQ(p[0], static_cast<uint16_t>(((0x7bde & 0x7fff) >> 1) + 0x3def));
    OBL_CHECK_EQ(p[1], 0x3def); // a pixel of 1 has no half bits
    p = snapshot;
    ApplyFade(p.data(), 4, 1, 3, kMasks, kColors);
    OBL_CHECK_EQ(p[0], static_cast<uint16_t>(((0x739c & 0x7fff) >> 2) + 0x5ef7));
    p = snapshot;
    ApplyFade(p.data(), 4, 1, 1, kMasks, kColors);
    OBL_CHECK_EQ(p[0], static_cast<uint16_t>(((0x7bde & 0x7fff) >> 1) + 0x1ce7 + ((0x739c & 0x7fff) >> 2)));
}

namespace {

// 16 x 16 cells; a rectangle covers the cells [left, right) x [top, bottom).
using Grid = std::vector<int>;
void Paint(Grid& g, const Rect& r)
{
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = r.left; x < r.right; ++x) g[y * 16 + x] = 1;
}

struct Rng {
    uint32_t state;
    int Range(int low, int high)
    {
        state = state * 1664525u + 1013904223u;
        return low + static_cast<int>((state >> 8) % static_cast<uint32_t>(high - low + 1));
    }
};

} // namespace

OBL_TEST_CASE("renderer: AddDirtyRect stores, absorbs and cuts rectangles")
{
    Vec list;
    AddDirtyRect(list, 0, 0, 10, 10);
    OBL_CHECK_EQ(list.size(), 1u);
    AddDirtyRect(list, 2, 2, 5, 5); // inside: dropped
    OBL_CHECK_EQ(list.size(), 1u);
    AddDirtyRect(list, 20, 20, 30, 30); // apart: stored
    OBL_CHECK_EQ(list.size(), 2u);
    AddDirtyRect(list, 0, 0, 40, 40); // covers both: they go, only it stays
    OBL_REQUIRE(list.size() == 1u);
    OBL_CHECK_EQ(list.rects[0].right, 40);

    // A rectangle that overlaps the stored one with a corner is cut: only the part outside is stored.
    Vec corner;
    AddDirtyRect(corner, 0, 0, 10, 10);
    AddDirtyRect(corner, 5, 5, 15, 15);
    OBL_REQUIRE(corner.size() == 3u);
    OBL_CHECK_EQ(corner.rects[1].left, 10); // the right strip first: (c.right, top, right, bottom)
    OBL_CHECK_EQ(corner.rects[1].top, 5);
    OBL_CHECK_EQ(corner.rects[1].right, 15);
    OBL_CHECK_EQ(corner.rects[1].bottom, 15);
    OBL_CHECK_EQ(corner.rects[2].left, 5); // then the part below the stored one: (left, c.bottom, c.right, bottom)
    OBL_CHECK_EQ(corner.rects[2].top, 10);
    OBL_CHECK_EQ(corner.rects[2].right, 10);
    OBL_CHECK_EQ(corner.rects[2].bottom, 15);
}

OBL_TEST_CASE("renderer: the dirty list always covers exactly the union of what was added")
{
    Rng rng{99};
    size_t pieces = 0;
    for (int round = 0; round < 3000; ++round) {
        Vec list;
        Grid expected(16 * 16, 0);
        const int count = rng.Range(1, 8);
        for (int i = 0; i < count; ++i) {
            const int left = rng.Range(0, 14), top = rng.Range(0, 14);
            const Rect r{left, top, rng.Range(left + 1, 16), rng.Range(top + 1, 16)};
            Paint(expected, r);
            AddDirtyRect(list, r.left, r.top, r.right, r.bottom);
        }
        Grid actual(16 * 16, 0);
        for (const Rect& r : list.rects) {
            OBL_CHECK(r.left < r.right && r.top < r.bottom);
            Paint(actual, r);
        }
        pieces += list.size();
        if (actual != expected) {
            OBL_CHECK(actual == expected);
            return;
        }
    }
    OBL_CHECK(pieces > 3000u);
}

OBL_TEST_CASE("renderer: ClearRows fills exactly the counted pixels with the key")
{
    std::vector<uint16_t> bits(12, 1);
    ClearRows(bits.data(), 8, 0x3e0);
    for (size_t i = 0; i < bits.size(); ++i) OBL_CHECK_EQ(bits[i], i < 8 ? 0x3e0 : 1);
    ClearRows(bits.data(), 0, 7);
    OBL_CHECK_EQ(bits[0], 0x3e0);
}

OBL_TEST_CASE("renderer: CopyKeyed skips the key, honours both strides and a narrower width")
{
    constexpr uint16_t key = 0x3e0;
    // Source 4 wide (stride 4), 3 rows; the destination is 7 wide, so the rows land at stride 7.
    const std::vector<uint16_t> src = {1, key, 3, 4, key, 6, key, 8, 9, 10, 11, key};
    std::vector<uint16_t> dst(7 * 3, 100);
    CopyKeyed(src.data(), 4, dst.data() + 2, 7, 4, 3, key);
    const uint16_t expected[3][7] = {{100, 100, 1, 100, 3, 4, 100}, {100, 100, 100, 6, 100, 8, 100}, {100, 100, 9, 10, 11, 100, 100}};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 7; ++column) OBL_CHECK_EQ(dst[static_cast<size_t>(row * 7 + column)], expected[row][column]);

    // A sub-rectangle: columns 1..2 of a stride-4 source (the pitch is not twice the width of the copy).
    std::vector<uint16_t> narrow(2 * 2, 0);
    CopyKeyed(src.data() + 1, 4, narrow.data(), 2, 2, 2, key);
    OBL_CHECK_EQ(narrow[0], 0); // key
    OBL_CHECK_EQ(narrow[1], 3);
    OBL_CHECK_EQ(narrow[2], 6);
    OBL_CHECK_EQ(narrow[3], 0); // key

    // A key-coloured pixel never overwrites; a source equal to the key everywhere changes nothing; no rows copy nothing.
    std::vector<uint16_t> same(4, 5);
    const std::vector<uint16_t> allKey(4, key);
    CopyKeyed(allKey.data(), 2, same.data(), 2, 2, 2, key);
    OBL_CHECK_EQ(same[0] + same[3], 10);
    CopyKeyed(src.data(), 4, same.data(), 2, 2, 0, key);
    CopyKeyed(src.data(), 4, same.data(), 2, 2, -3, key);
    OBL_CHECK_EQ(same[0] + same[3], 10);
}

OBL_TEST_CASE("renderer: the pixel block wraps at 32 bits")
{
    OBL_CHECK_EQ(SurfaceBlockBytes(640, 480), 640u * 480u * 2u);
    OBL_CHECK_EQ(SurfacePitch(65), 130);
    OBL_CHECK_EQ(SurfaceBlockBytes(0, 100), 0u);
    OBL_CHECK_EQ(SurfaceBlockBytes(65536, 32768), 0u); // 2^31 pixels -> 2^32 bytes wraps in 32 bits
}

OBL_TEST_CASE("renderer: IsModeSupported maps the four supported widths to their flags")
{
    OBL_CHECK_EQ(ModeSupportedIndex(640), 0);
    OBL_CHECK_EQ(ModeSupportedIndex(800), 1);
    OBL_CHECK_EQ(ModeSupportedIndex(1024), 2);
    OBL_CHECK_EQ(ModeSupportedIndex(1280), 3);
    OBL_CHECK_EQ(ModeSupportedIndex(0), -1);
    OBL_CHECK_EQ(ModeSupportedIndex(1920), -1);
    OBL_CHECK_EQ(ModeSupportedIndex(-640), -1);
}

OBL_TEST_CASE("renderer: the Present fade climbs to 4, holds 20 frames, climbs back down and switches off")
{
    ui::RendererFade fade{};
    OBL_CHECK_EQ(StepFade(fade), -1);
    fade.m_active = 1;
    std::vector<int> modes;
    for (int frame = 0; frame < 100 && fade.m_active; ++frame) modes.push_back(StepFade(fade));
    std::vector<int> expected;
    for (int level = 0; level < 4; ++level) expected.insert(expected.end(), 3, level);
    expected.push_back(4); // reaching 4 starts the hold
    expected.insert(expected.end(), 20, 4); // holding
    for (int level = 4; level > 0; --level) expected.insert(expected.end(), 3, level);
    expected.push_back(0); // level 0 switches the fade off
    OBL_CHECK(modes == expected);
    OBL_CHECK_EQ(fade.m_reverse, 0);
    OBL_CHECK_EQ(fade.m_holding, 0);
    OBL_CHECK_EQ(fade.m_hold, 0);
    OBL_CHECK_EQ(StepFade(fade), -1);

    // At level 0 a left-over hold count drains one extra frame per step.
    ui::RendererFade drain{};
    drain.m_active = 1;
    drain.m_hold = 3;
    OBL_CHECK_EQ(StepFade(drain), 0);
    OBL_CHECK_EQ(drain.m_hold, 2);
    OBL_CHECK_EQ(drain.m_tick, 1);
}

OBL_TEST_CASE("renderer: IsRectVisible compares against the view edge scaled by the zoom")
{
    VisibleView view;
    view.x = 10;
    view.y = 20;
    view.width = 100;
    view.height = 50;
    // Zoom 1: the far edge is x + width = 110, y + height = 70; the near test is view.x <= x + width - srcX.
    OBL_CHECK(IsRectVisible(view, 110, 70, 0, 0, 5, 5, 0));
    OBL_CHECK(!IsRectVisible(view, 111, 70, 0, 0, 5, 5, 0));
    OBL_CHECK(!IsRectVisible(view, 110, 71, 0, 0, 5, 5, 0));
    OBL_CHECK(IsRectVisible(view, 5, 15, 0, 0, 5, 5, 0)); // right edge 10 touches view.x
    OBL_CHECK(!IsRectVisible(view, 4, 15, 0, 0, 5, 5, 0));
    OBL_CHECK(!IsRectVisible(view, 50, 0, 0, 0, 5, 5, 0)); // bottom edge 5 < view.y
    OBL_CHECK(IsRectVisible(view, 8, 30, 3, 0, 5, 5, 0)); // srcX shrinks the right edge to 8 + 5 - 3 = 10
    OBL_CHECK(!IsRectVisible(view, 7, 30, 3, 0, 5, 5, 0));
    view.zoomLevel = 2;
    OBL_CHECK(IsRectVisible(view, 210, 120, 0, 0, 5, 5, 0));
    OBL_CHECK(!IsRectVisible(view, 211, 120, 0, 0, 5, 5, 0));
    view.zoomLevel = 0;
    OBL_CHECK(IsRectVisible(view, 60, 45, 0, 0, 5, 5, 0));
    OBL_CHECK(!IsRectVisible(view, 61, 45, 0, 0, 5, 5, 0));
    // Direct mode is unscaled unless the overlay flag is also set.
    view.directMode = true;
    OBL_CHECK(!IsRectVisible(view, 111, 30, 0, 0, 5, 5, 0));
    view.overlay = true;
    OBL_CHECK(IsRectVisible(view, 60, 45, 0, 0, 5, 5, 0));
    OBL_CHECK(!IsRectVisible(view, 61, 45, 0, 0, 5, 5, 0));
    // An unrecognized zoom compares the stray address on both axes.
    view.directMode = false;
    view.zoomLevel = 3;
    OBL_CHECK(IsRectVisible(view, 1000, 1000, 0, 0, 5, 5, 1000));
    OBL_CHECK(!IsRectVisible(view, 1001, 0, 0, 0, 5, 5, 1000));
    OBL_CHECK(!IsRectVisible(view, 0, 1001, 0, 0, 5, 5, 1000));
}

OBL_TEST_CASE("renderer: IntersectRects counts touching edges and writes the output only on overlap")
{
    const screen::Rect a{0, 0, 10, 10};
    screen::Rect out{-1, -2, -3, -4};
    OBL_CHECK(IntersectRects(a, {5, 6, 20, 30}, out));
    OBL_CHECK(out.left == 5 && out.top == 6 && out.right == 10 && out.bottom == 10);
    OBL_CHECK(IntersectRects(a, {10, 10, 12, 12}, out));
    OBL_CHECK(out.left == 10 && out.top == 10 && out.right == 10 && out.bottom == 10);
    out = {-1, -2, -3, -4};
    OBL_CHECK(!IntersectRects(a, {11, 0, 20, 10}, out));
    OBL_CHECK(!IntersectRects(a, {0, 11, 10, 20}, out));
    OBL_CHECK(!IntersectRects({11, 0, 20, 10}, a, out));
    OBL_CHECK(out.left == -1 && out.top == -2 && out.right == -3 && out.bottom == -4);
    OBL_CHECK(IntersectRects({-5, -5, 3, 3}, a, out));
    OBL_CHECK(out.left == 0 && out.top == 0 && out.right == 3 && out.bottom == 3);
}

OBL_TEST_CASE("renderer_ops: surface block size and pitch are 16-bit pixels with the 32-bit wrap")
{
    OBL_CHECK(SurfaceBlockBytes(640, 480) == 614400u);
    OBL_CHECK(SurfacePitch(640) == 1280);
    OBL_CHECK(SurfaceBlockBytes(0, 480) == 0u);
    OBL_CHECK(SurfaceBlockBytes(65536, 32768) == 0u); // 2^31 pixels * 2 wraps to 0
    OBL_CHECK(SurfacePitch(0x40000000) == static_cast<int>(0x80000000u));
}

OBL_TEST_CASE("renderer_ops: only the four supported display widths have a mode index")
{
    OBL_CHECK(ModeSupportedIndex(640) == 0);
    OBL_CHECK(ModeSupportedIndex(800) == 1);
    OBL_CHECK(ModeSupportedIndex(1024) == 2);
    OBL_CHECK(ModeSupportedIndex(1280) == 3);
    OBL_CHECK(ModeSupportedIndex(1920) == -1);
    OBL_CHECK(ModeSupportedIndex(0) == -1);
}
