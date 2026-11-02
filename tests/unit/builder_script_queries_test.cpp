#include "unit_test.h"

#include "game/construction/builder_script_actions.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

// Construction job rules (docs/specs/construction.md): the builder check for female guests routine and the builder find nearest valid tile routine.

using namespace obl;
using namespace obl::construction;

namespace {

struct Sent {
    int sender, receiver, type;
};

struct Fixture {
    using Object = const void*;
    struct Guest {
        int32_t tile[2];
        bool female;
    };
    std::vector<Guest> guests;
    std::vector<Object> list;
    int32_t builderTile[2] = {10, 10};
    int32_t whistling = 0;
    std::vector<Sent> sent;
    const void* builder = &builderTile;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> flags; // tile -> flags (missing = 0)
    uint32_t width = 16, height = 16;
    int tileSize = 64;

    const Object* GuestList(uint32_t& count)
    {
        list.clear();
        for (const Guest& guest : guests) list.push_back(&guest);
        count = static_cast<uint32_t>(list.size());
        return list.data();
    }
    void TilePosition(Object object, int32_t out[2])
    {
        const int32_t* tile = object == builder ? builderTile : static_cast<const Guest*>(object)->tile;
        out[0] = tile[0];
        out[1] = tile[1];
    }
    bool IsFemale(Object guest) { return static_cast<const Guest*>(guest)->female; }
    int32_t& Whistling() { return whistling; }
    void Post(int sender, int receiver, int type) { sent.push_back({sender, receiver, type}); }

    uint32_t Width() { return width; }
    uint32_t Height() { return height; }
    int TileSize() { return tileSize; }
    uint32_t CellFlags(uint32_t x, uint32_t y)
    {
        const auto it = flags.find({x % width, y % height});
        return it == flags.end() ? 0u : it->second;
    }
};

constexpr uint32_t kGround = 0x80000000u;

} // namespace

OBL_TEST_CASE("builder_script.female_guests_without_guests_posts_the_no_guest_message")
{
    Fixture f;
    CheckForFemaleGuests(f.builder, 77, f);
    OBL_REQUIRE(f.sent.size() == 1u);
    OBL_CHECK_EQ(f.sent[0].sender, 77);
    OBL_CHECK_EQ(f.sent[0].receiver, 77);
    OBL_CHECK_EQ(f.sent[0].type, kMsgBuilderNoGuests);
    OBL_CHECK_EQ(f.whistling, 0);
}

OBL_TEST_CASE("builder_script.female_guests_whistles_at_close_women_up_to_the_counter_limit")
{
    Fixture f;
    f.guests = {{{12, 8}, true}, {{8, 12}, true}, {{10, 10}, true}, {{12, 12}, false}};
    CheckForFemaleGuests(f.builder, 5, f);
    OBL_REQUIRE(f.sent.size() == 2u); // two places only; the man and the third woman add nothing
    OBL_CHECK_EQ(f.sent[0].type, kMsgBuilderWhistle);
    OBL_CHECK_EQ(f.sent[1].type, kMsgBuilderWhistle);
    OBL_CHECK_EQ(f.whistling, 2);
}

OBL_TEST_CASE("builder_script.female_guests_distance_is_two_tiles_per_axis")
{
    Fixture f;
    f.guests = {{{13, 10}, true}, {{10, 7}, true}, {{12, 12}, true}};
    f.whistling = 1;
    CheckForFemaleGuests(f.builder, 1, f);
    OBL_REQUIRE(f.sent.size() == 1u); // 3 tiles away misses in either axis; the third guest takes the last place
    OBL_CHECK_EQ(f.sent[0].type, kMsgBuilderWhistle);
    OBL_CHECK_EQ(f.whistling, 2);
}

OBL_TEST_CASE("builder_script.female_guests_full_counter_means_no_whistle_and_the_other_message")
{
    Fixture f;
    f.guests = {{{10, 10}, true}};
    f.whistling = 2;
    CheckForFemaleGuests(f.builder, 1, f);
    OBL_REQUIRE(f.sent.size() == 1u);
    OBL_CHECK_EQ(f.sent[0].type, kMsgBuilderNoGuests);
    OBL_CHECK_EQ(f.whistling, 2);
}

OBL_TEST_CASE("builder_script.female_guests_wrapped_difference_is_absolute")
{
    Fixture f;
    f.builderTile[0] =
            INT32_MIN; // the difference to the guest overflows into the negative range: abs(INT_MIN) stays INT_MIN, which is <= 2.0
    f.guests = {{{0, 10}, true}};
    CheckForFemaleGuests(f.builder, 1, f);
    OBL_REQUIRE(f.sent.size() == 1u);
    OBL_CHECK_EQ(f.sent[0].type, kMsgBuilderWhistle);
}

OBL_TEST_CASE("builder_script.nearest_valid_tile_own_tile_wins")
{
    Fixture f;
    f.flags[{10, 10}] = kGround;
    int32_t out[2] = {-1, -1};
    OBL_CHECK(FindNearestValidTile(f.builder, f, out));
    OBL_CHECK_EQ(out[0], 32 * 10);
    OBL_CHECK_EQ(out[1], 64 * 10 - 32); // even column: moved up by half a tile
}

OBL_TEST_CASE("builder_script.nearest_valid_tile_body_bit_makes_a_tile_invalid")
{
    Fixture f;
    f.flags[{10, 10}] = kGround | 0x10;
    f.flags[{10, 11}] = kGround | 0x10;
    f.flags[{10, 12}] = kGround;
    int32_t out[2] = {-1, -1};
    OBL_CHECK(FindNearestValidTile(f.builder, f, out));
    OBL_CHECK_EQ(out[0], 320);
    OBL_CHECK_EQ(out[1], 12 * 64 - 32);
}

OBL_TEST_CASE("builder_script.nearest_valid_tile_search_only_walks_the_column_south_and_wraps")
{
    Fixture f;
    f.builderTile[0] = 11; // odd column: no shift
    f.builderTile[1] = 14;
    f.flags[{11, 2}] = kGround; // 4 steps south of row 14 on a map of 16 rows
    f.flags[{9, 15}] = kGround; // a valid tile beside the column is never looked at
    int32_t out[2] = {-1, -1};
    OBL_CHECK(FindNearestValidTile(f.builder, f, out));
    OBL_CHECK_EQ(out[0], 32 * 11);
    OBL_CHECK_EQ(out[1], 2 * 64);
}

OBL_TEST_CASE("builder_script.nearest_valid_tile_gives_up_when_the_column_has_no_valid_cell")
{
    Fixture f;
    f.flags[{3, 3}] = kGround;
    int32_t out[2] = {-5, -6};
    OBL_CHECK(!FindNearestValidTile(f.builder, f, out));
    OBL_CHECK_EQ(out[0], -5);
    OBL_CHECK_EQ(out[1], -6);
}
