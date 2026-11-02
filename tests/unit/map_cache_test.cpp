#include "unit_test.h"
#include "game/ui/map_cache.h"

#include <cstring>
#include <memory>
#include <vector>

using namespace obl;
namespace {
namespace cache = ui::map_cache;
using ui::MapCache;

constexpr uint32_t kSide = map_pack::kIslandSide;
constexpr uint32_t kBlock = map_pack::kIslandBlockSide;

uint16_t Colour(uint32_t x, uint32_t y)
{
    return static_cast<uint16_t>(x * 31 + y * 17 + 5);
}
uint16_t Depth(uint32_t x, uint32_t y)
{
    return static_cast<uint16_t>(0x8000 + x * 7 + y * 11);
}

const map_pack::IslandPlanes& Island()
{
    static const map_pack::IslandPlanes planes = [] {
        map_pack::IslandPlanes built;
        built.colour.resize(kSide * kSide);
        built.depth.resize(kSide * kSide);
        for (uint32_t y = 0; y < kSide; ++y)
            for (uint32_t x = 0; x < kSide; ++x) {
                built.colour[y * kSide + x] = Colour(x, y);
                built.depth[y * kSide + x] = Depth(x, y);
            }
        return built;
    }();
    return planes;
}

struct Env {
    const map_pack::IslandPlanes* planes = &Island();
    bool rgb555 = false, openFailed = false;
    int closes = 0;
    bool Open(const char*) { return planes != nullptr; }
    void CouldNotOpen(const char*) { openFailed = true; }
    const map_pack::IslandPlanes* Planes() { return planes; }
    void Close() { ++closes; }
    bool Rgb555() { return rgb555; }
};

std::unique_ptr<MapCache> Built(Env& env)
{
    auto map = std::make_unique<MapCache>();
    std::memset(map.get(), 0xa5, sizeof(MapCache));
    cache::Construct(*map, "data/Scenario/scenario1/scenario1.biz", env);
    return map;
}

uint16_t To555Word(uint16_t word)
{
    cache::ConvertToRgb555(&word, 1);
    return word;
}

size_t BlockMismatches(uint32_t x, uint32_t y, const uint16_t* colour, const uint16_t* depth, bool rgb555)
{
    size_t mismatches = 0;
    for (uint32_t by = 0; by < kBlock; ++by)
        for (uint32_t bx = 0; bx < kBlock; ++bx) {
            const uint32_t px = x * kBlock + bx, py = y * kBlock + by;
            mismatches += colour[by * kBlock + bx] != (rgb555 ? To555Word(Colour(px, py)) : Colour(px, py));
            mismatches += depth[by * kBlock + bx] != Depth(px, py);
        }
    return mismatches;
}
} // namespace

OBL_TEST_CASE("biz_map.construct_empties_the_slots_and_leaves_no_file")
{
    Env env;
    auto map = Built(env);
    OBL_CHECK(!env.openFailed);
    OBL_CHECK_EQ(map->m_nextSlot, 0);
    for (const auto& slot : map->m_slots) {
        OBL_CHECK_EQ(slot.m_x, 0xffffffffu);
        OBL_CHECK_EQ(slot.m_y, 0xffffffffu);
    }
}

OBL_TEST_CASE("biz_map.construct_reports_missing_planes_and_goes_on")
{
    Env env;
    env.planes = nullptr;
    auto map = std::make_unique<MapCache>();
    std::memset(map.get(), 0, sizeof(MapCache));
    cache::Construct(*map, "missing/scenario1.biz", env);
    OBL_CHECK(env.openFailed);
}

OBL_TEST_CASE("biz_map.close_releases_the_planes")
{
    Env env;
    auto map = Built(env);
    cache::Close(*map, env);
    OBL_CHECK_EQ(env.closes, 1);
}

OBL_TEST_CASE("biz_map.load_block_copies_the_block_from_the_planes")
{
    Env env;
    auto map = Built(env);
    cache::LoadBlock(*map, 3, 2, env);
    OBL_CHECK_EQ(map->m_nextSlot, 0);
    OBL_CHECK_EQ(map->m_slots[0].m_x, 3u);
    OBL_CHECK_EQ(map->m_slots[0].m_y, 2u);
    OBL_CHECK_EQ(BlockMismatches(3, 2, map->m_slots[0].m_colour, map->m_slots[0].m_depth, false), 0u);
    OBL_CHECK_EQ(map->m_slots[1].m_x, 0xffffffffu);
}

OBL_TEST_CASE("biz_map.load_block_converts_only_the_colour_plane_to_555")
{
    Env env;
    env.rgb555 = true;
    auto map = Built(env);
    cache::LoadBlock(*map, 31, 31, env);
    OBL_CHECK_EQ(BlockMismatches(31, 31, map->m_slots[0].m_colour, map->m_slots[0].m_depth, true), 0u);
    OBL_CHECK_EQ(To555Word(0xf81f), 0x7c1f);
}

OBL_TEST_CASE("biz_map.load_block_without_planes_keeps_the_slot_words")
{
    Env env;
    env.planes = nullptr;
    auto map = Built(env);
    cache::LoadBlock(*map, 1, 1, env);
    OBL_CHECK_EQ(map->m_slots[0].m_x, 1u);
    OBL_CHECK_EQ(map->m_slots[0].m_colour[7], 0xa5a5);
    OBL_CHECK_EQ(map->m_slots[0].m_depth[7], 0xa5a5);
}

OBL_TEST_CASE("biz_map.get_block_hits_and_misses_cycle_through_four_slots")
{
    Env env;
    auto map = Built(env);
    uint16_t* first = cache::GetBlock<false>(*map, 1, 1, env);
    OBL_CHECK(first == map->m_slots[0].m_colour);
    OBL_CHECK_EQ(map->m_nextSlot, 1);
    OBL_CHECK(cache::GetBlock<true>(*map, 1, 1, env) == map->m_slots[0].m_depth);
    OBL_CHECK_EQ(map->m_nextSlot, 1);
    for (uint32_t i = 2; i <= 4; ++i) cache::GetBlock<false>(*map, i, 0, env);
    OBL_CHECK_EQ(map->m_nextSlot, 0);
    OBL_CHECK(cache::GetBlock<false>(*map, 5, 5, env) == map->m_slots[0].m_colour);
    OBL_CHECK_EQ(cache::FindSlot(*map, 1, 1), -1);
    OBL_CHECK_EQ(cache::FindSlot(*map, 3, 0), 2);
}

OBL_TEST_CASE("biz_map.next_slot_is_a_signed_modulo")
{
    OBL_CHECK_EQ(cache::GetNextSlot(0), 1);
    OBL_CHECK_EQ(cache::GetNextSlot(3), 0);
    OBL_CHECK_EQ(cache::GetNextSlot(7), 0);
    OBL_CHECK_EQ(cache::GetNextSlot(-1), 0);
    OBL_CHECK_EQ(cache::GetNextSlot(-2), -1);
    OBL_CHECK_EQ(cache::GetNextSlot(-5), -0);
}

OBL_TEST_CASE("biz_map.load_column_copies_32_blocks_colour_then_depth")
{
    Env env;
    auto map = Built(env);
    std::vector<uint16_t> out(32 * 0x8000, 0);
    cache::LoadColumn(*map, 4, out.data(), env);
    for (uint32_t x = 0; x < 32; ++x) {
        const uint16_t* colour = out.data() + x * 0x8000;
        OBL_CHECK_EQ(BlockMismatches(x, 4, colour, colour + 0x4000, false), 0u);
    }
}

OBL_TEST_CASE("biz_map.load_column_converts_each_colour_plane_in_555")
{
    Env env;
    env.rgb555 = true;
    auto map = Built(env);
    std::vector<uint16_t> out(32 * 0x8000, 0);
    cache::LoadColumn(*map, 0, out.data(), env);
    OBL_CHECK_EQ(BlockMismatches(0, 0, out.data(), out.data() + 0x4000, true), 0u);
    OBL_CHECK_EQ(out[0x8000 + 0x4000 + 1], Depth(128 + 1, 0));
}
