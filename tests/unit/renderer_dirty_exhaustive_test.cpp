// The renderer 16 add dirty rect routine (src/platform/renderer_dirty.h): every pair and a seeded set of triples of rectangles on a small grid.
// renderer_ops_test.cpp checks the union invariant on random 16x16 sets, which never reaches the rarer corner/edge cuts; here
// each stored/new combination is tried, so every branch of the cutting code runs and must still leave exactly the union.

#include "unit_test.h"

#include "platform/renderer_dirty.h"

#include <vector>

using obl::renderer_ops::AddDirtyRect;
using obl::screen::Rect;

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

constexpr int kSide = 7;
using Grid = std::vector<int>;

void Paint(Grid& g, const Rect& r)
{
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = r.left; x < r.right; ++x) g[static_cast<size_t>(y * kSide + x)] += 1;
}

std::vector<Rect> AllRects()
{
    std::vector<Rect> all;
    for (int l = 0; l < kSide; ++l)
        for (int r = l + 1; r <= kSide; ++r)
            for (int t = 0; t < kSide; ++t)
                for (int b = t + 1; b <= kSide; ++b) all.push_back({l, t, r, b});
    return all;
}

// The list must cover exactly the union, with no cell covered twice (the algorithm cuts, it does not overlap) and no empty piece.
bool Exact(const Vec& list, const Grid& expected)
{
    Grid actual(kSide * kSide, 0);
    for (const Rect& r : list.rects) {
        if (r.left >= r.right || r.top >= r.bottom) return false;
        Paint(actual, r);
    }
    for (size_t i = 0; i < actual.size(); ++i)
        if ((actual[i] != 0) != (expected[i] != 0)) return false;
    return true;
}

} // namespace

OBL_TEST_CASE("renderer_dirty.every_pair_keeps_the_union")
{
    const std::vector<Rect> all = AllRects();
    int failures = 0;
    for (const Rect& a : all) {
        for (const Rect& b : all) {
            Vec list;
            Grid expected(kSide * kSide, 0);
            AddDirtyRect(list, a.left, a.top, a.right, a.bottom);
            AddDirtyRect(list, b.left, b.top, b.right, b.bottom);
            Paint(expected, a);
            Paint(expected, b);
            if (!Exact(list, expected) && failures++ < 3)
                ::obl::unit::ReportFailure(__FILE__, __LINE__,
                        "union broken for (" + std::to_string(a.left) + "," + std::to_string(a.top) + "," + std::to_string(a.right) + "," +
                                std::to_string(a.bottom) + ") then (" + std::to_string(b.left) + "," + std::to_string(b.top) + "," +
                                std::to_string(b.right) + "," + std::to_string(b.bottom) + ")");
        }
    }
    OBL_CHECK_EQ(failures, 0);
}

OBL_TEST_CASE("renderer_dirty.seeded_triples_keep_the_union")
{
    const std::vector<Rect> all = AllRects();
    uint32_t state = 12345;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return all[(state >> 8) % all.size()];
    };
    int failures = 0;
    for (int round = 0; round < 20000; ++round) {
        Vec list;
        Grid expected(kSide * kSide, 0);
        for (int i = 0; i < 3; ++i) {
            const Rect r = next();
            AddDirtyRect(list, r.left, r.top, r.right, r.bottom);
            Paint(expected, r);
        }
        if (!Exact(list, expected)) ++failures;
    }
    OBL_CHECK_EQ(failures, 0);
}
