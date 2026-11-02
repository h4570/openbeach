

#include "unit_test.h"

#include "game/ui/map_restore.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace obl::map_restore;

namespace {

constexpr int kBs = 128;
constexpr int kSide = 4;

uint16_t ColourOf(int bx, int by, int pixel)
{
    return static_cast<uint16_t>(0x1000 + bx * 0x111 + by * 0x333 + pixel * 7);
}
uint16_t DepthOf(int bx, int by, int pixel)
{
    return static_cast<uint16_t>(0x4000 + bx * 0x101 + by * 0x203 + pixel * 5);
}

class FakeEnv {
public:
    FakeEnv()
    {
        for (int by = 0; by < kSide; ++by) {
            for (int bx = 0; bx < kSide; ++bx) {
                m_views.emplace_back(kBs * kBs, 0u);
                std::vector<uint16_t> depth(kBs * kBs), colour(kBs * kBs);
                for (int p = 0; p < kBs * kBs; ++p) {
                    depth[p] = DepthOf(bx, by, p);
                    colour[p] = ColourOf(bx, by, p);
                }
                m_depth.push_back(std::move(depth));
                m_colour.push_back(std::move(colour));
            }
        }
        m_loaded.assign(kSide * kSide, false);

        for (int i = 0; i < kSide * kSide; ++i)
            for (int p = 0; p < kBs * kBs; ++p) m_views[i][p] = (static_cast<uint32_t>(m_depth[i][p]) << 16) | 0xabcd;
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
    const uint16_t* Depth(uint32_t x, uint32_t y)
    {
        m_log.push_back("depth " + std::to_string(x) + "," + std::to_string(y));
        return m_depth[y * kSide + x].data();
    }
    const uint16_t* Colour(uint32_t x, uint32_t y)
    {
        m_log.push_back("colour " + std::to_string(x) + "," + std::to_string(y));
        return m_colour[y * kSide + x].data();
    }

    uint32_t& Word(int worldX, int worldY) { return m_views[(worldY / kBs) * kSide + worldX / kBs][(worldY % kBs) * kBs + worldX % kBs]; }
    uint16_t DepthAt(int worldX, int worldY) const
    {
        return m_depth[(worldY / kBs) * kSide + worldX / kBs][(worldY % kBs) * kBs + worldX % kBs];
    }
    uint16_t ColourAt(int worldX, int worldY) const
    {
        return m_colour[(worldY / kBs) * kSide + worldX / kBs][(worldY % kBs) * kBs + worldX % kBs];
    }
    void Poison(int worldX, int worldY, uint16_t depth) { Word(worldX, worldY) = (static_cast<uint32_t>(depth) << 16) | 0xabcd; }

    std::vector<std::vector<uint32_t>> m_views;
    std::vector<std::vector<uint16_t>> m_depth, m_colour;
    std::vector<bool> m_loaded;
    std::vector<std::string> m_log;
};

constexpr uint32_t kUntouched(uint32_t depth)
{
    return (depth << 16) | 0xabcd;
}

} // namespace

OBL_TEST_CASE("map_restore.rect_repaints_exactly_its_pixels")
{
    FakeEnv env;
    const PixelRect rect{130, 5, 140, 9};
    RestoreRect(env, rect);
    int changed = 0;
    for (int y = 0; y < kSide * kBs; ++y) {
        for (int x = 0; x < kSide * kBs; ++x) {
            const bool inside = x >= 130 && x < 140 && y >= 5 && y < 9;
            const uint32_t word = env.Word(x, y);
            if (inside) {
                OBL_CHECK_EQ(word, (static_cast<uint32_t>(env.DepthAt(x, y)) << 16) | env.ColourAt(x, y));
                ++changed;
            } else {
                OBL_CHECK_EQ(word, kUntouched(env.DepthAt(x, y)));
            }
        }
    }
    OBL_CHECK_EQ(changed, 40);
}

OBL_TEST_CASE("map_restore.rect_crosses_block_borders_and_asks_the_bizmap_depth_first")
{
    FakeEnv env;
    RestoreRect(env, PixelRect{120, 124, 136, 132});

    std::vector<std::string> maps;
    for (const std::string& line : env.m_log)
        if (line.rfind("map", 0) == 0) maps.push_back(line);
    OBL_CHECK_EQ(maps.size(), 4u);
    OBL_CHECK_EQ(env.m_log[0], std::string("map 0,0"));
    OBL_CHECK_EQ(env.m_log[1], std::string("depth 0,0"));
    OBL_CHECK_EQ(env.m_log[2], std::string("colour 0,0"));
    OBL_CHECK_EQ(env.Word(135, 131), (static_cast<uint32_t>(env.DepthAt(135, 131)) << 16) | env.ColourAt(135, 131));
    OBL_CHECK_EQ(env.Word(136, 131), kUntouched(env.DepthAt(136, 131)));
    OBL_CHECK_EQ(env.Word(119, 124), kUntouched(env.DepthAt(119, 124)));
}

OBL_TEST_CASE("map_restore.rect_keeps_the_cached_block_between_pixels_and_rows")
{
    FakeEnv env;
    RestoreRect(env, PixelRect{10, 10, 20, 14});
    int fetches = 0;
    for (const std::string& line : env.m_log) fetches += line.rfind("depth", 0) == 0;
    OBL_CHECK_EQ(fetches, 1);
}

OBL_TEST_CASE("map_restore.rect_with_nothing_to_walk_touches_nothing")
{
    for (const PixelRect& rect :
            {PixelRect{50, 50, 50, 60}, PixelRect{50, 50, 40, 60}, PixelRect{50, 50, 60, 50}, PixelRect{50, 50, 60, 20}}) {
        FakeEnv env;
        RestoreRect(env, rect);
        OBL_CHECK(env.m_log.empty());
    }
}

OBL_TEST_CASE("map_restore.rect_maps_blocks_that_are_not_loaded_once")
{
    FakeEnv env;
    env.m_loaded[0] = true;
    RestoreRect(env, PixelRect{100, 100, 130, 110});
    int maps = 0;
    for (const std::string& line : env.m_log) maps += line.rfind("map", 0) == 0;
    OBL_CHECK_EQ(maps, 1);
    OBL_CHECK(env.m_loaded[1]);
}

OBL_TEST_CASE("map_restore.tile_split_truncates_toward_zero")
{
    OBL_CHECK(SplitTilePosition(300, 515, 128) == (TileSplit{2, 4, 44, 3}));
    OBL_CHECK(SplitTilePosition(127, 128, 128) == (TileSplit{0, 1, 127, 0}));
    OBL_CHECK(SplitTilePosition(0, 0, 128) == (TileSplit{0, 0, 0, 0}));
    OBL_CHECK(SplitTilePosition(-1, -129, 128) == (TileSplit{0, -1, -1, -1}));
}

namespace {

std::vector<int8_t> MaskOf(const std::vector<std::vector<int>>& rows)
{
    std::vector<int8_t> mask;
    for (const auto& row : rows) {
        for (int token : row) {
            mask.push_back(static_cast<int8_t>(token));
            if (token > 0)
                for (int i = 0; i < token; ++i) mask.push_back(static_cast<int8_t>(0x55));
        }
    }
    mask.resize(mask.size() + 8, 0);
    return mask;
}

void ReferenceTile(FakeEnv& env, uint32_t blockX, uint32_t blockY, int offsetX, int offsetY, const std::vector<std::vector<int>>& rows)
{
    for (size_t r = 0; r < rows.size(); ++r) {
        int column = 0;
        for (int token : rows[r]) {
            if (token < 0) {
                column -= token;
                continue;
            }
            for (int i = 0; i < token; ++i, ++column) {
                const int worldX = static_cast<int>((blockX * kBs + offsetX + column) % (kSide * kBs));
                const int worldY = static_cast<int>(blockY * kBs) + offsetY + static_cast<int>(r);
                uint32_t& word = env.Word(worldX, worldY);
                if (static_cast<uint16_t>(word >> 16) == env.DepthAt(worldX, worldY))
                    word = env.ColourAt(worldX, worldY) + (word & 0xffff0000u);
            }
        }
    }
}

std::vector<std::vector<int>> RandomRows(uint32_t& seed)
{
    const auto next = [&seed](int low, int high) {
        seed = seed * 1664525u + 1013904223u;
        return low + static_cast<int>((seed >> 8) % static_cast<uint32_t>(high - low + 1));
    };
    std::vector<std::vector<int>> rows;
    for (int r = 0; r < kTileRows; ++r) {
        std::vector<int> row;
        int consumed = 0;
        while (consumed < kTileColumns) {
            const int kind = next(0, 9);
            if (kind == 0) {
                row.push_back(0);
            } else if (kind < 5) {
                const int skip = next(0, 7) == 0 ? 128 : next(1, 40);
                row.push_back(-skip);
                consumed += skip;
            } else {
                const int run = next(0, 7) == 0 ? 127 : next(1, 40);
                row.push_back(run);
                consumed += run;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace

OBL_TEST_CASE("map_restore.tile_restores_only_unpainted_run_pixels")
{
    FakeEnv env;
    const std::vector<std::vector<int>> rows(kTileRows, std::vector<int>{-10, 52});

    env.Poison(12 + 10 + 3, 20, 0x1234);
    const uint32_t kept = env.Word(25, 20);
    TileStats stats;
    RestoreTileRect(env, 0, 0, 12, 20, MaskOf(rows).data(), &stats);
    OBL_CHECK_EQ(env.Word(25, 20), kept);
    OBL_CHECK_EQ(stats.kept, 1u);
    OBL_CHECK_EQ(stats.restored, 52u * 33u - 1u);

    OBL_CHECK_EQ(env.Word(12, 20), kUntouched(env.DepthAt(12, 20)));
    OBL_CHECK_EQ(env.Word(22, 20), (static_cast<uint32_t>(env.DepthAt(22, 20)) << 16) | env.ColourAt(22, 20));
    OBL_CHECK_EQ(env.Word(22, 52), (static_cast<uint32_t>(env.DepthAt(22, 52)) << 16) | env.ColourAt(22, 52));
    OBL_CHECK_EQ(env.Word(22, 53), kUntouched(env.DepthAt(22, 53)));
}

OBL_TEST_CASE("map_restore.tile_fetches_the_first_block_depth_first_then_colour_first")
{
    FakeEnv env;
    RestoreTileRect(env, 1, 1, 0, 0, MaskOf(std::vector<std::vector<int>>(kTileRows, std::vector<int>{62})).data());
    OBL_REQUIRE(env.m_log.size() >= 6);
    OBL_CHECK_EQ(env.m_log[0], std::string("depth 1,1"));
    OBL_CHECK_EQ(env.m_log[1], std::string("colour 1,1"));
    OBL_CHECK_EQ(env.m_log[2], std::string("map 1,1"));
    OBL_CHECK_EQ(env.m_log[3], std::string("colour 1,1"));
    OBL_CHECK_EQ(env.m_log[4], std::string("depth 1,1"));
}

OBL_TEST_CASE("map_restore.tile_follows_runs_into_the_next_block_and_wraps_the_last_one")
{
    for (const int blockX : {0, 2, kSide - 1}) {
        FakeEnv env, reference;
        const std::vector<std::vector<int>> rows(kTileRows, std::vector<int>{62});
        RestoreTileRect(env, blockX, 1, kBs - 20, 100, MaskOf(rows).data());
        ReferenceTile(reference, blockX, 1, kBs - 20, 100, rows);
        OBL_CHECK(env.m_views == reference.m_views);
        OBL_CHECK(env.m_loaded[1 * kSide + blockX]);
        OBL_CHECK(env.m_loaded[1 * kSide + (blockX + 1) % kSide]);
        OBL_CHECK(env.m_loaded[2 * kSide + (blockX + 1) % kSide]);
    }
}

OBL_TEST_CASE("map_restore.tile_skips_that_end_on_the_block_edge_and_beyond")
{
    for (const int skip : {19, 20, 21, 61}) {
        FakeEnv env, reference;
        const std::vector<std::vector<int>> rows(kTileRows, std::vector<int>{-skip, 62});
        RestoreTileRect(env, 0, 0, kBs - 20, 0, MaskOf(rows).data());
        ReferenceTile(reference, 0, 0, kBs - 20, 0, rows);
        OBL_CHECK(env.m_views == reference.m_views);
    }
}

OBL_TEST_CASE("map_restore.tile_matches_the_reference_on_random_masks")
{
    uint32_t seed = 7;
    for (int round = 0; round < 60; ++round) {
        FakeEnv env, reference;
        const std::vector<std::vector<int>> rows = RandomRows(seed);
        seed = seed * 1664525u + 1013904223u;
        const uint32_t blockX = (seed >> 8) % kSide;
        const uint32_t blockY = (seed >> 12) % (kSide - 1);
        const int offsetX = static_cast<int>((seed >> 16) % kBs);
        const int offsetY = static_cast<int>((seed >> 20) % kBs);

        for (int i = 0; i < 6000; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const int x = static_cast<int>((seed >> 8) % (kSide * kBs));
            const int y = static_cast<int>((seed >> 4) % (kSide * kBs));
            env.Poison(x, y, 0x0100);
            reference.Poison(x, y, 0x0100);
        }
        RestoreTileRect(env, blockX, blockY, offsetX, offsetY, MaskOf(rows).data());
        ReferenceTile(reference, blockX, blockY, offsetX, offsetY, rows);
        OBL_CHECK(env.m_views == reference.m_views);
    }
}

OBL_TEST_CASE("map_restore.ensure_mapped_tests_the_flag_with_both_strides")
{
    struct Env {
        int perRow = 3, perColumn = 5;
        std::map<int, bool> loaded;
        std::vector<std::string> log;
        int BlocksPerRow() const { return perRow; }
        int BlocksPerColumn() const { return perColumn; }
        bool Loaded(int index) { return loaded[index]; }
        void Map(uint32_t x, uint32_t y)
        {
            loaded[static_cast<int>(y) * perRow + static_cast<int>(x)] = true;
            log.push_back("map " + std::to_string(x) + "," + std::to_string(y));
        }
    } env;
    EnsureMapped(env, 2, 1);
    OBL_CHECK_EQ(env.log.size(), 1u);
    env.loaded.clear();
    env.loaded[2 * 5 + 1] = true;
    EnsureMapped(env, 1, 2);
    OBL_CHECK_EQ(env.log.size(), 2u);
    EnsureMapped(env, 1, 2);
    OBL_CHECK_EQ(env.log.size(), 2u);
}
