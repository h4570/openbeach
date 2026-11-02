// Playlist state operations (src/game/ui/playlist_logic.h): the behavior read from Playlist_*.

#include "unit_test.h"

#include "game/ui/playlist_logic.h"
#include "game/ui/playlist_nav.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace obl::playlist;

namespace {

struct Track {
    const char* file = "";
    const char* directory = "";
    int night = 0;
    uint8_t repeat = 0;
    uint8_t marked = 0;
};

struct List {
    std::vector<Track> storage;
    std::vector<Track*> items;
    int current = 0;
    explicit List(int count)
    {
        storage.resize(static_cast<size_t>(count));
        for (Track& track : storage) items.push_back(&track);
    }
    int Count() const { return static_cast<int>(items.size()); }
    int IndexOf(const Track* track) const { return static_cast<int>(track - storage.data()); }
    std::string Order() const
    {
        std::string text;
        for (const Track* track : items) text += static_cast<char>('0' + IndexOf(track));
        return text;
    }
};

} // namespace

OBL_TEST_CASE("playlist.clamp_index")
{
    OBL_CHECK_EQ(ClampIndex(3, 5), 3);
    OBL_CHECK_EQ(ClampIndex(5, 5), 4);
    OBL_CHECK_EQ(ClampIndex(99, 5), 4);
    OBL_CHECK_EQ(ClampIndex(-7, 5), 0);
    OBL_CHECK_EQ(ClampIndex(0, 0), 0); // empty list: count - 1 = -1 clamps back to 0
    OBL_CHECK_EQ(ClampIndex(4, 0), 0);
}

OBL_TEST_CASE("playlist.set_repeat_keeps_a_single_repeating_track")
{
    List list(3);
    SetRepeat(list.items.data(), 3, 0, 1);
    SetRepeat(list.items.data(), 3, 2, 1);
    OBL_CHECK_EQ(int(list.storage[0].repeat), 0);
    OBL_CHECK_EQ(int(list.storage[2].repeat), 1);
    SetRepeat(list.items.data(), 3, 2, 0); // clearing leaves the others alone
    SetRepeat(list.items.data(), 3, 1, 1);
    SetRepeat(list.items.data(), 3, 0, 0);
    OBL_CHECK_EQ(int(list.storage[1].repeat), 1);
    OBL_CHECK_EQ(int(list.storage[0].repeat), 0);
}

OBL_TEST_CASE("playlist.shuffle_draws_modulo_position_plus_one")
{
    List list(4);
    std::vector<int> draws;
    const int tape[] = {7, 4, 1};
    size_t used = 0;
    Shuffle(list.items.data(), 4, [&] {
        draws.push_back(tape[used]);
        return tape[used++];
    });
    OBL_CHECK_EQ(used, size_t(3)); // one draw per track after the first
    // i=1: 7%2=1 (self), i=2: 4%3=1 swaps 2<->1, i=3: 1%4=1 swaps 3<->1
    OBL_CHECK_EQ(list.Order(), std::string("0312"));
    List none(1);
    Shuffle(none.items.data(), 1, [&]() -> int {
        OBL_CHECK(false);
        return 0;
    });
    List empty(0);
    Shuffle(empty.items.data(), 0, [&]() -> int {
        OBL_CHECK(false);
        return 0;
    });
}

OBL_TEST_CASE("playlist.move_up_and_down_carry_the_current_index")
{
    List list(4);
    list.current = 2;
    MoveUp(list.items.data(), list.current, 2); // the playing track moves with its position
    OBL_CHECK_EQ(list.Order(), std::string("0213"));
    OBL_CHECK_EQ(list.current, 1);
    MoveUp(list.items.data(), list.current, 2); // the neighbour of the playing track steps over it
    OBL_CHECK_EQ(list.Order(), std::string("0123"));
    OBL_CHECK_EQ(list.current, 2);
    MoveUp(list.items.data(), list.current, 0); // first track: nothing
    OBL_CHECK_EQ(list.Order(), std::string("0123"));

    list.current = 0;
    MoveDown(list.items.data(), 4, list.current, 0);
    OBL_CHECK_EQ(list.Order(), std::string("1023"));
    OBL_CHECK_EQ(list.current, 1);
    MoveDown(list.items.data(), 4, list.current, 0);
    OBL_CHECK_EQ(list.Order(), std::string("0123"));
    OBL_CHECK_EQ(list.current, 0);
    list.current = 3;
    MoveDown(list.items.data(), 4, list.current, 3); // last track: nothing
    OBL_CHECK_EQ(list.Order(), std::string("0123"));
    OBL_CHECK_EQ(list.current, 3);
    MoveDown(list.items.data(), 4, list.current, 1); // unrelated current index stays
    OBL_CHECK_EQ(list.Order(), std::string("0213"));
    OBL_CHECK_EQ(list.current, 3);
}

OBL_TEST_CASE("playlist.remove_marked_keeps_order_and_releases_the_rest")
{
    List list(5);
    list.storage[0].marked = 1;
    list.storage[3].marked = 1;
    list.storage[4].marked = 1;
    std::vector<int> released;
    const int kept = RemoveMarked(list.items.data(), 5, [&](Track* track) { released.push_back(list.IndexOf(track)); });
    OBL_CHECK_EQ(kept, 2);
    OBL_CHECK_EQ(list.IndexOf(list.items[0]), 1);
    OBL_CHECK_EQ(list.IndexOf(list.items[1]), 2);
    OBL_CHECK_EQ(released.size(), size_t(3));
    OBL_CHECK_EQ(released[0], 0);
    OBL_CHECK_EQ(released[2], 4);
    List clean(2);
    OBL_CHECK_EQ(RemoveMarked(clean.items.data(), 2, [&](Track*) { OBL_CHECK(false); }), 2);
}

OBL_TEST_CASE("playlist.plan_delete_moves_the_current_index_and_the_reselected_row")
{
    const int rows[] = {1, 2, 5};
    auto plan = PlanDelete(rows, 3, 4);
    OBL_CHECK_EQ(plan.index, 2);
    OBL_CHECK(!plan.currentDeleted);
    OBL_CHECK_EQ(plan.lastRow, 5);
    OBL_CHECK_EQ(plan.reselect, 3);
    plan = PlanDelete(rows, 3, 2);
    OBL_CHECK_EQ(plan.index, 1);
    OBL_CHECK(plan.currentDeleted);
    const int one[] = {7};
    plan = PlanDelete(one, 1, 3);
    OBL_CHECK_EQ(plan.index, 3);
    OBL_CHECK_EQ(plan.reselect, 7);
    // Rows in selection order, not sorted: the comparison runs against the running index.
    const int pair[] = {2, 1};
    OBL_CHECK_EQ(PlanDelete(pair, 2, 3).index, 1);
    OBL_CHECK_EQ(PlanDelete(pair, 2, 2).index, 1);
}

OBL_TEST_CASE("playlist.contains_needs_both_strings_exactly")
{
    List list(2);
    list.storage[0].directory = "music\\";
    list.storage[0].file = "a.mp3";
    list.storage[1].directory = "data\\soundtrack\\";
    list.storage[1].file = "Day1.mp3";
    OBL_CHECK(Contains(list.items.data(), 2, "data\\soundtrack\\", "Day1.mp3"));
    OBL_CHECK(!Contains(list.items.data(), 2, "music\\", "Day1.mp3"));
    OBL_CHECK(!Contains(list.items.data(), 2, "data\\soundtrack\\", "day1.mp3")); // case-sensitive
    OBL_CHECK(!Contains(list.items.data(), 0, "music\\", "a.mp3"));
}

OBL_TEST_CASE("playlist.mode_queries")
{
    List list(3);
    list.storage[1].night = 1;
    OBL_CHECK(HasMode(list.items.data(), 3, 0));
    OBL_CHECK(HasMode(list.items.data(), 3, 1));
    OBL_CHECK(!HasMode(list.items.data(), 3, 2));
    OBL_CHECK(HasOtherMode(list.items.data(), 3, 0));
    OBL_CHECK(HasOtherMode(list.items.data(), 3, 2));
    List days(2);
    OBL_CHECK(!HasOtherMode(days.items.data(), 2, 0));
    OBL_CHECK(!HasMode(days.items.data(), 0, 0));
    OBL_CHECK(!HasOtherMode(days.items.data(), 0, 0));
}

OBL_TEST_CASE("playlist.first_of_mode")
{
    List list(3);
    list.storage[1].night = 1;
    list.storage[2].night = 1;
    OBL_CHECK_EQ(FirstOfMode(list.items.data(), 3, 0), 0);
    OBL_CHECK_EQ(FirstOfMode(list.items.data(), 3, 1), 1);
    OBL_CHECK_EQ(FirstOfMode(list.items.data(), 3, 2), -1);
    OBL_CHECK_EQ(FirstOfMode(list.items.data(), 0, 0), -1);
}

OBL_TEST_CASE("playlist.navigate_every_track_wraps_and_clamps")
{
    List list(3);
    int current = 2;
    NavResult next = Navigate(list.items.data(), 3, current, 2, true, +1);
    OBL_CHECK(next.play);
    OBL_CHECK_EQ(current, 0);
    current = 0;
    NavResult previous = Navigate(list.items.data(), 3, current, 2, true, -1);
    OBL_CHECK(previous.play);
    OBL_CHECK_EQ(current, 2);
    OBL_CHECK(!Navigate(list.items.data(), 0, current, 2, true, +1).play);
}

OBL_TEST_CASE("playlist.navigate_repeat_keeps_the_track")
{
    List list(3);
    list.storage[1].repeat = 1;
    int current = 1;
    NavResult result = Navigate(list.items.data(), 3, current, 0, false, +1); // plays even when not `playing`
    OBL_CHECK(result.play);
    OBL_CHECK_EQ(current, 1);
}

OBL_TEST_CASE("playlist.navigate_filter_skips_other_time_of_day")
{
    List list(5);
    list.storage[2].night = 1;
    list.storage[4].night = 1;
    int current = 0;
    NavResult result = Navigate(list.items.data(), 5, current, 1, true, +1);
    OBL_CHECK(result.play);
    OBL_CHECK_EQ(current, 2);
    result = Navigate(list.items.data(), 5, current, 1, true, +1);
    OBL_CHECK_EQ(current, 4);
    result = Navigate(list.items.data(), 5, current, 1, true, +1); // wraps past the day tracks
    OBL_CHECK_EQ(current, 2);
    result = Navigate(list.items.data(), 5, current, 1, true, -1);
    OBL_CHECK_EQ(current, 4);
    // Not playing: the walk moves but nothing starts.
    current = 0;
    result = Navigate(list.items.data(), 5, current, 1, false, +1);
    OBL_CHECK(!result.play);
    OBL_CHECK_EQ(current, 2);
}

OBL_TEST_CASE("playlist.navigate_filter_without_match_closes_the_stream")
{
    List list(3); // all day tracks, the filter asks for night
    int current = 1;
    NavResult result = Navigate(list.items.data(), 3, current, 1, true, +1);
    OBL_CHECK(result.closeStream);
    OBL_CHECK(!result.play);
    OBL_CHECK_EQ(current, 1); // a full circle ends on the starting track
    result = Navigate(list.items.data(), 3, current, 1, false, -1);
    OBL_CHECK(result.closeStream);
    OBL_CHECK(!result.play);
}
