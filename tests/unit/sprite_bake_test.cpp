
#include "unit_test.h"

#include "game/ui/sprite_bake.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace obl::sprite_bake;

namespace {

constexpr int kBs = 128;
constexpr int kSide = 4; // blocks per row and column of the fake map

struct Lcg {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int Range(int low, int high) { return low + static_cast<int>(Next() % static_cast<uint32_t>(high - low + 1)); }
    bool Chance(int percent) { return static_cast<int>(Next() % 100) < percent; }
};

// The game's Env over plain memory: a table of block views, a log of the maps, and a tile mask of 62 x 33 bytes.
class FakeEnv {
public:
    FakeEnv()
    {
        for (int i = 0; i < kSide * kSide; ++i) m_views.emplace_back(kBs * kBs, 0u);
        m_loaded.assign(kSide * kSide, false);
        for (int i = 0; i < kTileMaskStride * 40; ++i) m_mask.push_back(static_cast<uint8_t>((i * 7 + i / 62) % 3 == 0 ? 0 : 1));
    }

    int BlocksPerRow() const { return kSide; }
    int BlocksPerColumn() const { return kSide; }
    int BlockSize() const { return kBs; }
    bool Loaded(int index) { return m_loaded[index]; }
    void Map(uint32_t x, uint32_t y)
    {
        m_loaded[y * kSide + x] = true;
        m_log.push_back("map " + std::to_string(x) + "," + std::to_string(y));
    }
    uint32_t* View(uint32_t x, uint32_t y) { return m_views[y * kSide + x].data(); }
    uint8_t Mask(int index) const { return m_mask[static_cast<size_t>(index)]; }

    uint32_t& Word(int worldX, int worldY) { return m_views[(worldY / kBs) * kSide + worldX / kBs][(worldY % kBs) * kBs + worldX % kBs]; }
    void Fill(Lcg& rng, uint32_t centre)
    {
        for (auto& view : m_views)
            for (auto& word : view) {
                const uint32_t high =
                        rng.Chance(40) ? (rng.Next() & 0xffff) : ((centre + static_cast<uint32_t>(rng.Range(-3, 3))) & 0xffff);
                word = (high << 16) | (rng.Next() & 0xffff);
            }
    }

    std::vector<std::vector<uint32_t>> m_views;
    std::vector<bool> m_loaded;
    std::vector<uint8_t> m_mask;
    std::vector<std::string> m_log;
};

// A decoded frame next to its run-length streams: the colour stream (control bytes, palette-index payloads) and a depth stream with the same runs.
struct Frame {
    int width = 0, height = 0;
    std::vector<uint8_t> colour, depth;
    std::vector<std::vector<int>> opaque; // [row][column]
    std::vector<std::vector<uint8_t>> pixelColour, pixelDepth;
};

Frame MakeFrame(Lcg& rng, int w, int h)
{
    Frame f;
    f.width = w;
    f.height = h;
    f.opaque.assign(h, std::vector<int>(w, 0));
    f.pixelColour.assign(h, std::vector<uint8_t>(w, 0));
    f.pixelDepth.assign(h, std::vector<uint8_t>(w, 0));
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w;) {
            const int n = std::min(w - col, 1 + static_cast<int>(rng.Next() % (rng.Chance(10) ? 127 : 12)));
            const bool literal = (rng.Next() & 3) != 0;
            f.colour.push_back(static_cast<uint8_t>(literal ? n : -n));
            f.depth.push_back(static_cast<uint8_t>(literal ? n : -n));
            for (int k = 0; k < n; ++k) {
                if (literal) {
                    const uint8_t c = static_cast<uint8_t>(rng.Next()), d = static_cast<uint8_t>(rng.Next());
                    f.colour.push_back(c);
                    f.depth.push_back(d);
                    f.opaque[row][col + k] = 1;
                    f.pixelColour[row][col + k] = c;
                    f.pixelDepth[row][col + k] = d;
                }
            }
            col += n;
        }
    }
    f.colour.resize(f.colour.size() + 16, 0x7f);
    f.depth.resize(f.depth.size() + 16, 0x7f);
    return f;
}

std::vector<uint16_t> MakePalette(Lcg& rng)
{
    std::vector<uint16_t> palette(256);
    for (auto& v : palette) v = static_cast<uint16_t>(rng.Next());
    return palette;
}

constexpr uint16_t kHalf = 0xf7de,
                   kQuarter = 0xe79c; // the RGB565 masks of the blit get mask half routine / the blit get mask quarter routine

uint16_t Blend(unsigned mode, uint16_t s, uint16_t d)
{
    if (mode == 0x80) return static_cast<uint16_t>(((d >> 1) & (kHalf >> 1)) + ((s >> 1) & (kHalf >> 1)));
    const uint16_t major = mode == 0x40 ? s : d;
    return static_cast<uint16_t>(((major >> 1) & (kHalf >> 1)) + ((d >> 2) & (kQuarter >> 2)) + ((s >> 2) & (kQuarter >> 2)));
}

uint32_t RefDepth(uint8_t byte, float step, uint32_t base)
{
    const double product = std::trunc(static_cast<double>(byte) * static_cast<double>(step));
    const uint32_t low = static_cast<uint32_t>(static_cast<int64_t>(product)) & 0xffffu;
    return ((((low + (base & 0xffffu)) & 0xffffu) << 16) | 0x10000u);
}

} // namespace

OBL_TEST_CASE("sprite_bake.depth_matches_a_per_pixel_model")
{
    Lcg rng{7};
    for (int round = 0; round < 60; ++round) {
        const int w = rng.Range(1, 40), h = rng.Range(1, 30);
        Frame frame = MakeFrame(rng, w, h);
        const std::vector<uint16_t> palette = MakePalette(rng);
        FakeEnv env, ref;
        const uint32_t base = rng.Next() & 0xffff;
        env.Fill(rng, base);
        ref.m_views = env.m_views;
        PixelRect rect{rng.Chance(30) ? rng.Range(0, w) : 0, rng.Chance(30) ? rng.Range(0, h) : 0, 0, 0};
        rect.x1 = std::min(w + 2, rect.x0 + rng.Range(0, w + 2));
        rect.y1 = std::min(h, rect.y0 + rng.Range(0, h + 2));
        const int bx = rng.Range(0, 1), by = rng.Range(0, 1);
        const int ox = rng.Chance(50) ? rng.Range(kBs - 30, kBs - 1) : rng.Range(0, kBs - 1);
        const int oy = rng.Chance(50) ? rng.Range(kBs - 20, kBs - 1) : rng.Range(0, kBs - 1);
        const float steps[] = {0.0f, 0.25f, 1.0f, -1.5f, 3.0f, 100.5f, -77.7f};
        const float step = steps[rng.Next() % std::size(steps)];

        BakeDepth(env, rect, bx, by, ox, oy, step, base, DepthFrames{frame.colour.data(), frame.depth.data(), palette.data(), w});

        for (int row = rect.y0; row < rect.y1; ++row) {
            for (int col = std::max(rect.x0, 0); col < std::min(rect.x1, w); ++col) {
                if (!frame.opaque[row][col]) continue;
                const int x = bx * kBs + ox + col, y = by * kBs + oy + row;
                const uint32_t word = RefDepth(frame.pixelDepth[row][col], step, base);
                uint32_t& target = ref.Word(x, y);
                if ((target & 0xffff0000u) <= word) target = palette[frame.pixelColour[row][col]] + word;
            }
        }
        OBL_CHECK(env.m_views == ref.m_views);
    }
}

OBL_TEST_CASE("sprite_bake.depth_word_follows_the_truncation_of_the_depth_byte_times_the_step")
{
    OBL_CHECK_EQ(DepthWord(10, 1.5f, 0x1234), ((((15u + 0x1234u) & 0xffffu) << 16) | 0x10000u));
    OBL_CHECK_EQ(DepthWord(0, 5.0f, 0x00ff), 0x00ff0000u | 0x10000u);
    // The low bit of the depth is forced on: an even sum gets +1, an odd one stays.
    OBL_CHECK_EQ(DepthWord(2, 1.0f, 0x0100) >> 16, 0x0103u);
    OBL_CHECK_EQ(DepthWord(3, 1.0f, 0x0100) >> 16, 0x0103u);
    // Truncation toward zero, 16-bit wrap of the sum, and the integer-indefinite result for NaN and infinity (zero extent bytes make both).
    OBL_CHECK_EQ(DepthWord(3, -1.5f, 0) >> 16, static_cast<uint32_t>((static_cast<uint16_t>(-4)) | 1u));
    OBL_CHECK_EQ(DepthWord(255, 300.0f, 0xff00) >> 16, ((76500u + 0xff00u) & 0xffffu) | 1u);
    const float inf = std::numeric_limits<float>::infinity();
    OBL_CHECK_EQ(DepthWord(5, inf, 0x0200), 0x02000000u | 0x10000u);
    OBL_CHECK_EQ(DepthWord(0, inf, 0x0200), 0x02000000u | 0x10000u); // 0 * inf = NaN
}

OBL_TEST_CASE("sprite_bake.depth_skips_rows_and_maps_each_block_once")
{
    Lcg rng{11};
    Frame frame = MakeFrame(rng, 20, 12);
    const std::vector<uint16_t> palette = MakePalette(rng);
    FakeEnv env;
    // The frame's 12 rows start 4 rows above a block border and 10 columns left of one: up to four blocks are touched.
    BakeDepth(env, PixelRect{0, 0, 20, 12}, 1, 1, kBs - 10, kBs - 4, 1.0f, 0x100,
            DepthFrames{frame.colour.data(), frame.depth.data(), palette.data(), 20});
    // Each block is mapped once, however many pixels fall into it (the flag test passes after the first map).
    std::map<std::string, int> maps;
    for (const std::string& line : env.m_log) ++maps[line];
    OBL_CHECK(maps.size() >= 2 && maps.size() <= 4);
    for (const auto& entry : maps) OBL_CHECK_EQ(entry.second, 1);
    // A window that starts at row 5 skips five rows of the stream first: the painted rows are the same pixels as in the full bake.
    FakeEnv full, window;
    BakeDepth(full, PixelRect{0, 0, 20, 12}, 1, 1, 10, 10, 1.0f, 0x100,
            DepthFrames{frame.colour.data(), frame.depth.data(), palette.data(), 20});
    BakeDepth(window, PixelRect{0, 5, 20, 12}, 1, 1, 10, 10, 1.0f, 0x100,
            DepthFrames{frame.colour.data(), frame.depth.data(), palette.data(), 20});
    int differing = 0;
    for (size_t b = 0; b < full.m_views.size(); ++b)
        for (size_t i = 0; i < full.m_views[b].size(); ++i) {
            const int row = static_cast<int>(i) / kBs;
            if (full.m_views[b][i] != window.m_views[b][i]) ++differing;
            // Rows 10..14 of the block are frame rows 0..4: they are only in the full bake.
            if (b == (1 * kSide + 1) && row >= 10 + 5) OBL_CHECK_EQ(full.m_views[b][i], window.m_views[b][i]);
        }
    OBL_CHECK(differing > 0);
}

OBL_TEST_CASE("sprite_bake.empty_or_inverted_windows_paint_nothing")
{
    Lcg rng{13};
    Frame frame = MakeFrame(rng, 16, 8);
    const std::vector<uint16_t> palette = MakePalette(rng);
    for (const PixelRect& rect : {PixelRect{0, 0, 0, 8}, PixelRect{5, 0, 3, 8}, PixelRect{0, 4, 16, 4}, PixelRect{0, 6, 16, 2}}) {
        FakeEnv env;
        BakeDepth(env, rect, 0, 0, 3, 3, 1.0f, 0x10, DepthFrames{frame.colour.data(), frame.depth.data(), palette.data(), 16});
        for (const auto& view : env.m_views)
            for (uint32_t word : view) OBL_CHECK_EQ(word, 0u);
    }
}

OBL_TEST_CASE("sprite_bake.blend_modes_weigh_source_and_destination")
{
    // Source white over black in RGB565: half is 0x7bef, three quarters 0x7bef + 0x39e7.
    OBL_CHECK_EQ(BlendColour(0x80, 0xffff, 0x0000, kHalf, kQuarter), 0x7befu);
    OBL_CHECK_EQ(BlendColour(0x80, 0x0000, 0xffff, kHalf, kQuarter), 0x7befu);
    OBL_CHECK_EQ(BlendColour(0x40, 0xffff, 0x0000, kHalf, kQuarter), 0xb5d6u);
    OBL_CHECK_EQ(BlendColour(0xc0, 0xffff, 0x0000, kHalf, kQuarter), 0x39e7u);
    OBL_CHECK_EQ(BlendColour(0xc0, 0x0000, 0xffff, kHalf, kQuarter), 0xb5d6u);
    OBL_CHECK_EQ(BlendColour(0x40, 0x0000, 0xffff, kHalf, kQuarter), 0x39e7u);
}

OBL_TEST_CASE("sprite_bake.colour_matches_a_per_pixel_model_including_the_depth_ramp")
{
    Lcg rng{17};
    for (int round = 0; round < 60; ++round) {
        const int w = rng.Range(1, 40), h = rng.Range(1, 30);
        Frame frame = MakeFrame(rng, w, h);
        const std::vector<uint16_t> palette = MakePalette(rng);
        FakeEnv env, ref;
        const uint32_t depth = rng.Next() & 0xffff;
        const int hotspot = rng.Range(-20, 20);
        env.Fill(rng, (depth + static_cast<uint32_t>(rng.Range(-20, 20))) & 0xffff);
        ref.m_views = env.m_views;
        PixelRect rect{rng.Chance(30) ? rng.Range(0, w) : 0, rng.Chance(30) ? rng.Range(0, h) : 0, 0, 0};
        rect.x1 = std::min(w + 2, rect.x0 + rng.Range(0, w + 2));
        rect.y1 = std::min(h, rect.y0 + rng.Range(0, h + 2));
        const int bx = rng.Range(0, 1), by = rng.Range(0, 1);
        const int ox = rng.Chance(50) ? rng.Range(kBs - 30, kBs - 1) : rng.Range(0, kBs - 1);
        const int oy = rng.Chance(50) ? rng.Range(kBs - 20, kBs - 1) : rng.Range(0, kBs - 1);
        BakeColour(env, rect, bx, by, ox, oy, depth, ColourFrame{frame.colour.data(), palette.data(), w, hotspot, kHalf, kQuarter});

        uint32_t rowDepth = ((depth & 0xffffu) + static_cast<uint32_t>(hotspot) * 0xfff8u) << 16;
        int counter = hotspot;
        for (int row = 0; row < rect.y1; ++row) {
            rowDepth = counter < 0 ? rowDepth - 0x80000u : rowDepth + 0x100000u;
            ++counter;
            if (row < rect.y0) continue;
            for (int col = std::max(rect.x0, 0); col < std::min(rect.x1, w); ++col) {
                if (!frame.opaque[row][col]) continue;
                const int x = bx * kBs + ox + (col - rect.x0), y = by * kBs + oy + (row - rect.y0);
                uint32_t& target = ref.Word(x, y);
                if ((target & 0xffff0000u) > rowDepth) continue;
                const uint8_t byte = frame.pixelColour[row][col];
                const unsigned mode = byte & 0xc0u;
                const uint16_t source = palette[byte & 0x3fu];
                target = mode == 0 ? source + rowDepth
                                   : static_cast<uint16_t>(Blend(mode, source, static_cast<uint16_t>(target))) + (target & 0xffff0000u);
            }
        }
        OBL_CHECK(env.m_views == ref.m_views);
    }
}

OBL_TEST_CASE("sprite_bake.rect_colour_positions_from_the_frame_origin_and_gates_with_the_tile_mask")
{
    Lcg rng{19};
    for (int round = 0; round < 60; ++round) {
        const int w = rng.Range(1, 30), h = rng.Range(1, 20);
        Frame frame = MakeFrame(rng, w, h);
        const std::vector<uint16_t> palette = MakePalette(rng);
        FakeEnv env, ref;
        const uint32_t depth = rng.Next() & 0xffff;
        const int hotspot = rng.Range(-10, 10);
        env.Fill(rng, depth);
        ref.m_views = env.m_views;
        PixelRect rect{rng.Chance(30) ? rng.Range(0, w) : 0, rng.Chance(30) ? rng.Range(0, h) : 0, 0, 0};
        rect.x1 = std::min(w, rect.x0 + rng.Range(0, w + 1));
        rect.y1 = std::min(h, rect.y0 + rng.Range(0, h + 1));
        const int bx = rng.Range(0, 1), by = rng.Range(0, 1);
        const int ox = rng.Range(kBs - 25, kBs - 1), oy = rng.Range(0, kBs - 1);
        const int maskX = rng.Range(0, 20), maskY = rng.Range(0, 10);

        BakeRectColour(env, rect, bx, by, ox, oy, depth, maskX, maskY,
                ColourFrame{frame.colour.data(), palette.data(), w, hotspot, kHalf, kQuarter});

        uint32_t rowDepth = ((depth & 0xffffu) + static_cast<uint32_t>(hotspot) * 0xfff8u) << 16;
        int counter = hotspot;
        for (int row = 0; row < rect.y1; ++row) {
            rowDepth = counter < 0 ? rowDepth - 0x80000u : rowDepth + 0x100000u;
            ++counter;
            if (row < rect.y0) continue;
            for (int col = std::max(rect.x0, 0); col < std::min(rect.x1, w); ++col) {
                if (!frame.opaque[row][col]) continue;
                // Unlike BakeBuildingColor the position is the frame origin's plus the frame coordinates, not measured from the window's corner.
                const int x = bx * kBs + ox + col, y = by * kBs + oy + row;
                uint32_t& target = ref.Word(x, y);
                if ((target & 0xffff0000u) > rowDepth) continue;
                if (!env.Mask(((row - rect.y0) + maskY) * kTileMaskStride - rect.x0 + col + maskX)) continue;
                const uint8_t byte = frame.pixelColour[row][col];
                const unsigned mode = byte & 0xc0u;
                const uint16_t source = palette[byte & 0x3fu];
                target = mode == 0 ? source + rowDepth
                                   : static_cast<uint16_t>(Blend(mode, source, static_cast<uint16_t>(target))) + (target & 0xffff0000u);
            }
        }
        OBL_CHECK(env.m_views == ref.m_views);
    }
}

OBL_TEST_CASE("sprite_bake.writer_position_moves_the_window_and_splits_the_origin")
{
    const WriterPosition p = ToWriterPosition(PixelRect{300, 500, 380, 560}, 290, 515, 128);
    OBL_CHECK(p == (WriterPosition{PixelRect{10, -15, 90, 45}, 2, 4, 34, 3}));
    // Signed division and remainder: a frame origin left of or above the map truncates toward zero.
    const WriterPosition n = ToWriterPosition(PixelRect{0, 0, 10, 10}, -5, -130, 128);
    OBL_CHECK(n == (WriterPosition{PixelRect{5, 130, 15, 140}, 0, -1, -5, -2}));
}

OBL_TEST_CASE("sprite_bake.rect_depth_is_the_point_depth_plus_eight_made_odd")
{
    OBL_CHECK_EQ(RectDepth(100), 109u);
    OBL_CHECK_EQ(RectDepth(101), 109u);
    OBL_CHECK_EQ(RectDepth(0xfffffff7u), 0xffffffffu);
    OBL_CHECK_EQ(RectDepth(0xfffffff8u), 1u); // wraps, then the even sum 0 becomes 1
}

OBL_TEST_CASE("sprite_bake.overlay_clip_preserves_frame_edges_and_the_fixed_map_limit")
{
    const auto negative = ClipFrame(-12, -7, 80, 30, true);
    OBL_CHECK(negative.clipped);
    OBL_CHECK_EQ(negative.x, 0);
    OBL_CHECK_EQ(negative.y, 0);
    OBL_CHECK(negative.rect == (PixelRect{12, 7, 80, 30}));
    const auto edge = ClipFrame(4070, 4080, 80, 30, true);
    OBL_CHECK(edge.clipped);
    OBL_CHECK(edge.rect == (PixelRect{0, 0, 26, 16}));
    const auto ghost = ClipFrame(4070, 4080, 80, 30, false);
    OBL_CHECK(!ghost.clipped);
    OBL_CHECK(ghost.rect == (PixelRect{0, 0, 80, 30}));
    OBL_CHECK(!ClipFrame(4016, 4066, 80, 30, true).clipped);
}

OBL_TEST_CASE("sprite_bake.ghost_matches_decoded_pixels_and_wraps_block_columns")
{
    Lcg rng{123};
    for (int trial = 0; trial < 40; ++trial) {
        const int w = rng.Range(1, 290), h = rng.Range(1, 25);
        const auto f = MakeFrame(rng, w, h);
        const auto palette = MakePalette(rng);
        const uint32_t depth = rng.Next() & 0xffffu;
        const float step = static_cast<float>(rng.Range(-200, 200)) / 7.0f;
        FakeEnv actual;
        actual.Fill(rng, depth);
        FakeEnv expected = actual;
        const int bx = trial % kSide, by = 1, ox = rng.Range(100, 127), oy = rng.Range(110, 127);
        const int top = trial % h;
        const PixelRect rect{w - 1, top, 0, h}; // horizontal edges are deliberately inverted
        BakeGhost(actual, rect, bx, by, ox, oy, step, depth, {f.colour.data(), f.depth.data(), palette.data(), w});
        for (int row = top; row < h; ++row)
            for (int column = 0; column < w; ++column) {
                if (!f.opaque[row][column]) continue;
                const int x = (bx * kBs + ox + column) % (kSide * kBs);
                const int y = by * kBs + oy + row - top;
                uint32_t& target = expected.Word(x, y);
                const uint32_t word = DepthWord(f.pixelDepth[row][column], step, depth);
                if ((target & 0xffff0000u) <= word) target = word + palette[f.pixelColour[row][column]];
            }
        OBL_CHECK(actual.m_views == expected.m_views);
    }
}

OBL_TEST_CASE("sprite_bake.ghost_maps_transparent_rows_and_zero_width_rows")
{
    FakeEnv env;
    const uint8_t colour[] = {static_cast<uint8_t>(-128), static_cast<uint8_t>(-12)};
    const uint16_t palette[256] = {};
    BakeGhost(env, {99, 0, 0, 1}, 3, 0, 120, 0, 1.0f, 0, {colour, colour, palette, 140});
    OBL_CHECK(env.Loaded(3));
    OBL_CHECK(env.Loaded(0));
    OBL_CHECK(env.Loaded(1)); // the second transparent run ends across another boundary
    OBL_CHECK_EQ(env.m_log.size(), 3u);
    BakeGhost(env, {0, 0, 0, 1}, 1, 2, 0, 0, 1.0f, 0, {colour, colour, palette, 0});
    OBL_CHECK(env.Loaded(9));
}

OBL_TEST_CASE("sprite_bake.ground_tile_matches_decoded_pixels_and_keeps_the_background_depth")
{
    Lcg rng{321};
    for (int trial = 0; trial < 60; ++trial) {
        const int w = rng.Range(1, 120), h = rng.Range(1, 30);
        const auto f = MakeFrame(rng, w, h);
        const auto palette = MakePalette(rng);
        const uint32_t depth = rng.Next() & 0xffffu;
        FakeEnv actual;
        actual.Fill(rng, depth + 8); // row limits climb by 0x10 per row, so some rows pass the depth test and some do not
        FakeEnv expected = actual;
        const int bx = trial % kSide, by = trial % 2, ox = rng.Range(60, 127), oy = rng.Range(90, 127);
        BakeGroundTile(actual, bx, by, ox, oy, depth, {f.colour.data(), palette.data(), w, h, kHalf, kQuarter});
        for (int row = 0; row < h; ++row)
            for (int column = 0; column < w; ++column) {
                if (!f.opaque[row][column]) continue;
                uint32_t& target = expected.Word((bx * kBs + ox + column) % (kSide * kBs), by * kBs + oy + row);
                const uint32_t limit = ((depth & 0xffffu) + 0x10u * static_cast<uint32_t>(row)) << 16;
                if ((target & 0xffff0000u) > limit) continue;
                const uint8_t payload = f.pixelColour[row][column];
                const unsigned mode = payload & 0xc0u;
                const uint16_t source = palette[payload - mode];
                target = (mode == 0 ? source : Blend(mode, source, static_cast<uint16_t>(target))) + (target & 0xffff0000u);
            }
        OBL_CHECK(actual.m_views == expected.m_views);
    }
}

OBL_TEST_CASE("sprite_bake.ground_tile_maps_every_row_and_wraps_skips_only_past_the_edge")
{
    FakeEnv env;
    const uint16_t palette[256] = {};
    // Row 0: one skip of 8 starting at x = 120 ends exactly on the edge (128), which does not wrap; the next row does the same at offset 121 and does wrap.
    const uint8_t colour[] = {static_cast<uint8_t>(-8), static_cast<uint8_t>(-8)};
    BakeGroundTile(env, 1, 0, 120, 0, 0, {colour, palette, 8, 1, kHalf, kQuarter});
    OBL_CHECK_EQ(env.m_log.size(), 1u);
    BakeGroundTile(env, 1, 0, 121, 0, 0, {colour, palette, 8, 1, kHalf, kQuarter});
    OBL_CHECK(env.Loaded(2));
    // A block row of an empty frame is still mapped, and a zero width maps without reading the stream.
    FakeEnv other;
    BakeGroundTile(other, 0, 1, 0, 0, 0, {colour, palette, 0, 3, kHalf, kQuarter});
    OBL_CHECK(other.Loaded(4));
    OBL_CHECK_EQ(other.m_log.size(), 1u);
}

OBL_TEST_CASE("Iso tile mask follows symmetric 2:1 diamond geometry")
{
    int area = 0;
    for (int y = 0; y < 32; ++y) {
        int row = 0;
        for (int x = 0; x < kTileMaskStride; ++x) {
            const auto value = IsoTileMask(y * kTileMaskStride + x);
            OBL_CHECK_EQ(value, IsoTileMask(y * kTileMaskStride + 61 - x));
            OBL_CHECK_EQ(value, IsoTileMask((31 - y) * kTileMaskStride + x));
            row += value;
        }
        OBL_CHECK_EQ(row, 2 + 4 * std::min(y, 31 - y));
        area += row;
    }
    OBL_CHECK_EQ(area, 1024);
    for (int x = 0; x < kTileMaskStride; ++x) OBL_CHECK_EQ(IsoTileMask(32 * kTileMaskStride + x), 0);
    OBL_CHECK_EQ(IsoTileMask(-1), 0);
    OBL_CHECK_EQ(IsoTileMask(kTileMaskStride * kTileMaskHeight), 0);
}
