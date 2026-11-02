// The pure parts of the small Scenery methods (src/game/objects/scenery_queries.h, docs/specs/scenery.md).

#include "unit_test.h"
#include "game/objects/scenery_names.h"

#include "game/objects/scenery_queries.h"

#include <cstdint>
#include <cstring>

using namespace obl::scenery;

OBL_TEST_CASE("scenery.has_state_is_0_1_3_4")
{
    OBL_CHECK(StateIsKnown(0));
    OBL_CHECK(StateIsKnown(1));
    OBL_CHECK(!StateIsKnown(2));
    OBL_CHECK(StateIsKnown(3));
    OBL_CHECK(StateIsKnown(4));
    OBL_CHECK(!StateIsKnown(5));
    OBL_CHECK(!StateIsKnown(0xffffffffu));
}

OBL_TEST_CASE("scenery.is_selectable_needs_a_flag_and_no_burn")
{
    OBL_CHECK(IsSelectable(false, true, false));
    OBL_CHECK(IsSelectable(false, false, true));
    OBL_CHECK(IsSelectable(false, true, true));
    OBL_CHECK(!IsSelectable(false, false, false));
    OBL_CHECK(!IsSelectable(true, true, true));
}

OBL_TEST_CASE("scenery.free_seat_count_wraps")
{
    OBL_CHECK_EQ(FreeSeatsLeft(2, 0), 2);
    OBL_CHECK_EQ(FreeSeatsLeft(2, 2), 0);
    OBL_CHECK_EQ(FreeSeatsLeft(0, 3), -3);
    OBL_CHECK_EQ(FreeSeatsLeft(INT32_MIN, 1), INT32_MAX);
}

OBL_TEST_CASE("scenery.bin_fill_clamps_above_seventy_percent")
{
    OBL_CHECK_EQ(ClampBinFill(0, 10), 0u);
    OBL_CHECK_EQ(ClampBinFill(7, 10), 7u); // 10 * 0.7 rounds to exactly 7.0 as a double
    OBL_CHECK_EQ(ClampBinFill(8, 10), 10u);
    OBL_CHECK_EQ(ClampBinFill(10, 10), 10u);
    OBL_CHECK_EQ(ClampBinFill(500, 10), 10u);
    OBL_CHECK_EQ(ClampBinFill(3, 0), 0u); // capacity 0: everything above 0 goes to 0
    OBL_CHECK_EQ(ClampBinFill(0, 0), 0u);
    OBL_CHECK_EQ(ClampBinFill(0xffffffffu, 10), 10u);
    OBL_CHECK_EQ(ClampBinFill(5, 5), 5u);
    OBL_CHECK_EQ(ClampBinFill(4, 5), 5u); // 3.5 < 4
    OBL_CHECK_EQ(ClampBinFill(3, 5), 3u);
}

OBL_TEST_CASE("scenery.bin_animation_only_for_bins_and_full_means_exactly_full")
{
    OBL_CHECK(BinFillAnim(false, 10, 10) == BinAnim::None);
    OBL_CHECK(BinFillAnim(true, 10, 10) == BinAnim::Full);
    OBL_CHECK(BinFillAnim(true, 9, 10) == BinAnim::Empty);
    OBL_CHECK(BinFillAnim(true, 0, 10) == BinAnim::Empty);
    OBL_CHECK(BinAnimName(BinAnim::None) == nullptr);
    OBL_CHECK(std::strcmp(BinAnimName(BinAnim::Full), "bin_fullbin_static") == 0);
    OBL_CHECK(std::strcmp(BinAnimName(BinAnim::Empty), "bin_emptybin_static") == 0);
}

OBL_TEST_CASE("scenery.copy_name_takes_the_terminator")
{
    char target[8];
    std::memset(target, 'x', sizeof(target));
    CopyName(target, "Bin");
    OBL_CHECK(std::strcmp(target, "Bin") == 0);
    OBL_CHECK_EQ(target[4], 'x');
}

OBL_TEST_CASE("scenery.bench_offsets_follow_seat_and_direction")
{
    SeatOffset a = BenchSeatOffset(0, 0);
    OBL_CHECK(a.defined);
    OBL_CHECK_EQ(a.x, -13.0f);
    OBL_CHECK_EQ(a.y, 20.0f);
    SeatOffset b = BenchSeatOffset(1, 7);
    OBL_CHECK_EQ(b.x, 6.0f);
    OBL_CHECK_EQ(b.y, 24.0f);
    SeatOffset c = BenchSeatOffset(2, 3);
    OBL_CHECK(c.defined);
    OBL_CHECK_EQ(c.x, 0.0f);
    OBL_CHECK_EQ(c.y, 0.0f);
    SeatOffset d = BenchSeatOffset(-1, 99); // any other seat ignores the direction
    OBL_CHECK(d.defined);
    OBL_CHECK(!BenchSeatOffset(0, 8).defined);
    OBL_CHECK(!BenchSeatOffset(1, 0xffffffffu).defined);
}

OBL_TEST_CASE("scenery.names_ignore_case_for_keys_and_catalog_names")
{
    OBL_CHECK(obl::scenery_names::IsBin("bin") && obl::scenery_names::IsBin("Bin") && !obl::scenery_names::IsBin("binder"));
    OBL_CHECK(obl::scenery_names::IsLounger("lounger") && obl::scenery_names::IsLounger("Lounger") &&
            !obl::scenery_names::IsLounger("bench"));
    OBL_CHECK(obl::scenery_names::IsLitter("litter") && obl::scenery_names::IsLitter("Litter") &&
            !obl::scenery_names::IsLitter("litterpuke"));
    OBL_CHECK(obl::scenery_names::HasLitterInName("litterpuke") && obl::scenery_names::HasLitterInName("Litter") &&
            !obl::scenery_names::HasLitterInName("bin"));
    OBL_CHECK(obl::scenery_names::IsHoarding("hoarding_big") && obl::scenery_names::IsHoarding("HOARDING_BIG") &&
            !obl::scenery_names::IsHoarding("hoarding"));
}
