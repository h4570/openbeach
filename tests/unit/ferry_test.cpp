#include "unit_test.h"
#include "game/ferry/ferry_logic.h"

#include <cstring>
#include <iterator>
#include <limits>

// The FerryManager's schedule, pending performances, arrivals and leavers on the plain class.
namespace {
struct Env {
    float hour = 10;
    uint32_t day = 0;
    int draws = 0;
    int Rand() { return ++draws; }
    float Hour() { return hour; }
    uint32_t Day() { return day; }
    int GuestCount() { return 2; }
    int GuestId(const char* key) { return key[0] == 'M' ? 0 : 1; }
    int Gender(int i) { return i; }
    int Rating() { return 50; }
    void Intensities(float& m, float& f)
    {
        m = 3;
        f = 1;
    }
};

// A fresh manager as the game's construct leaves it: no cursor until the first schedule boat, which the fixture sets to the end
// (the sentinel) so the cursor tests start from an exhausted schedule.
struct Fixture {
    Env env;
    obl::ferry::Manager m;
    Fixture() { m.cursor = m.schedule.end(); }
    void Add(int flags, float time, int day = 0, int info = 1)
    {
        obl::ferry::Boat b{};
        b.TimeFlags = flags;
        b.Time = time;
        b.Day = day;
        b.GuestInfo = info;
        b.NumGuests = 4;
        obl::ferry::AddBoatEvent(m, env, b);
    }
    void Add(const obl::ferry::Boat& b) { obl::ferry::AddBoatEvent(m, env, b); }
};

obl::ferry::Boat Boat(int info, int numGuests)
{
    obl::ferry::Boat b{};
    b.TimeFlags = 1;
    b.Time = 8;
    b.GuestInfo = info;
    b.NumGuests = numGuests;
    return b;
}
} // namespace

OBL_TEST_CASE("ferry.order_cursor_and_pending_guard")
{
    Fixture f;
    f.Add(3, 0);
    obl::ferry::Arrival out{};
    OBL_CHECK(!obl::ferry::PopDueBoat(f.m, f.env, out));
    f.Add(1, 12);
    f.Add(1, 8);
    f.Add(2, 8, 2);
    OBL_CHECK_EQ(f.m.cursor.value()->Day, 2);
    OBL_CHECK_EQ(f.m.performance.front().Time, 10);
    OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK(!obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK_EQ(f.m.cursor.value()->Day, 0);
    OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK_EQ(out.numGuests, 4);
    OBL_CHECK_EQ(out.guestTypes[0] + out.guestTypes[1], 4);
    OBL_CHECK(!obl::ferry::PopDueBoat(f.m, f.env, out));
}

OBL_TEST_CASE("ferry.insert_before_cursor_keeps_the_cursor_on_its_boat")
{
    Fixture f;
    f.Add(1, 5);
    f.Add(1, 9);
    OBL_CHECK_EQ(f.m.cursor.value()->Time, 5);
    f.Add(1, 7); // lands between the cursor's boat and the next one
    OBL_CHECK_EQ(f.m.cursor.value()->Time, 5);
    OBL_CHECK_EQ(std::distance(f.m.schedule.begin(), f.m.cursor.value()), 0);
}

OBL_TEST_CASE("ferry.one_shot_at_cursor_is_removed_and_the_cursor_moves_on")
{
    Fixture f;
    f.Add(0, 10);
    f.Add(1, 12);
    obl::ferry::Arrival out{};
    OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK_EQ(f.m.schedule.size(), size_t(1));
    OBL_CHECK_EQ(f.m.cursor.value()->Time, 12);
}

OBL_TEST_CASE("ferry.flag_three_boat_at_the_cursor_moves_the_cursor_on")
{
    Fixture f;
    obl::ferry::Boat due = Boat(1, 4);
    due.TimeFlags = 3;
    due.Time = 5;
    f.m.schedule.push_back(due);
    f.m.schedule.push_back(Boat(1, 4));
    f.m.schedule.back().Time = 9;
    f.m.cursor = f.m.schedule.begin();
    obl::ferry::Arrival out{};
    OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK_EQ(f.m.schedule.size(), size_t(1));
    OBL_CHECK_EQ(f.m.cursor.value()->Time, 9);
}

OBL_TEST_CASE("ferry.null_cursor_pops_a_pending_performance")
{
    obl::ferry::Manager m;
    Env env;
    obl::ferry::Boat boat = Boat(3, 4);
    boat.TimeFlags = 3;
    obl::ferry::AddBoatEvent(m, env, boat);
    obl::ferry::Arrival out{};
    OBL_CHECK(obl::ferry::PopDueBoat(m, env, out));
    OBL_CHECK(m.performance.empty());
    OBL_CHECK(!m.cursor.has_value());
}

OBL_TEST_CASE("ferry.time_queries_exact_hour_and_future_day")
{
    Fixture f;
    uint32_t day = 77;
    OBL_CHECK_EQ(obl::ferry::GetNextBoatTime(f.m, 0, 10, 0, day), -1);
    OBL_CHECK_EQ(day, 77u);
    f.Add(1, 10);
    f.Add(2, 12, 2);
    f.Add(0, 11, 1);
    OBL_CHECK_EQ(obl::ferry::GetNextBoatTime(f.m, 0, 10, 0, day), 10);
    OBL_CHECK_EQ(day, 1u);
    OBL_CHECK_EQ(obl::ferry::GetNextBoatTime(f.m, 0, 10, 1, day), -1);
    OBL_CHECK_EQ(day, 1u);
    OBL_CHECK_EQ(obl::ferry::GetBoatTimeToday(f.m, 0, 0), 10);
    OBL_CHECK_EQ(obl::ferry::GetBoatTimeToday(f.m, 0, 1), -1);
    OBL_CHECK_EQ(obl::ferry::GetBoatTimeToday(f.m, UINT32_MAX, 2), 12);
}

OBL_TEST_CASE("ferry.one_shot_removal_and_performance_rounding")
{
    Fixture f;
    f.Add(0, 10);
    obl::ferry::Arrival out{};
    OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK(f.m.schedule.empty());
    f.Add(1, 10, 0, 3);
    f.m.guests.fAvgHapp[0] = 2;
    f.m.guests.fAvgHapp[1] = 1;
    f.m.guests.iHappyLeavers = 4;
    OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK_EQ(out.performance, 1);
    OBL_CHECK_EQ(out.numGuests, 1);
    OBL_CHECK_EQ(f.m.lastLeft, 4);
    OBL_CHECK_EQ(f.m.lastArrived, 1);
    OBL_CHECK_EQ(f.m.guests.fAvgHapp[0], 0);
}

OBL_TEST_CASE("ferry.unordered_schedule_and_no_write_due_check")
{
    Fixture f;
    f.Add(1, 10);
    f.Add(1, std::numeric_limits<float>::quiet_NaN());
    OBL_CHECK_EQ(f.m.schedule.front().Time, 10);
    f.env.hour = std::numeric_limits<float>::quiet_NaN();
    obl::ferry::Arrival out{};
    out.numGuests = 77;
    OBL_CHECK(!obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK_EQ(out.numGuests, 77);
    OBL_CHECK_EQ(f.env.draws, 0);
    OBL_CHECK_EQ(obl::ferry::Truncate(std::numeric_limits<double>::quiet_NaN()), 0);
}

OBL_TEST_CASE("ferry_arrivals.random_count_uses_rand_modulo")
{
    Fixture f;
    f.Add(Boat(0, 4));
    f.env.draws = 2; // first draw 3 -> count 3 % 4; then types 4%2, 5%2, 6%2
    obl::ferry::Arrival out{};
    OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK_EQ(out.numGuests, 3);
    OBL_CHECK_EQ(out.guestTypes[0], 2);
    OBL_CHECK_EQ(out.guestTypes[1], 1);
    OBL_CHECK_EQ(f.env.draws, 6);
    OBL_CHECK_EQ(out.performance, 0);
}

OBL_TEST_CASE("ferry_arrivals.random_count_never_below_one")
{
    {
        Fixture f;
        f.Add(Boat(0, 4));
        f.env.draws = 3; // draw 4 % 4 == 0 -> forced to 1 guest
        obl::ferry::Arrival out{};
        OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
        OBL_CHECK_EQ(out.numGuests, 1);
        OBL_CHECK_EQ(f.env.draws, 5);
    }
    {
        Fixture f;
        f.Add(Boat(0, 0)); // no modulo by zero, and no draw for the count
        obl::ferry::Arrival out{};
        OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
        OBL_CHECK_EQ(out.numGuests, 1);
        OBL_CHECK_EQ(out.guestTypes[1], 1); // the single type draw is 1 % 2
        OBL_CHECK_EQ(f.env.draws, 1);
    }
}

OBL_TEST_CASE("ferry_arrivals.keyword_list_fixed_and_random_numbers")
{
    Fixture f;
    obl::ferry::Boat b = Boat(2, 0);
    std::strcpy(b.GuestKeyword[0], "Male");
    b.GuestType[0] = 0;
    b.GuestKeywordNumbers[0] = 3;
    std::strcpy(b.GuestKeyword[1], "Female");
    b.GuestType[1] = 1;
    b.GuestKeywordNumbers[1] = 5;
    b.GuestKeywordNumbers[2] = 9; // beyond the type count: never read
    std::strcpy(b.GuestKeyword[2], "Male");
    f.Add(b);
    obl::ferry::Arrival out{};
    out.numGuests = 99; // overwritten, not accumulated
    OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK_EQ(out.guestTypes[0], 3);
    OBL_CHECK_EQ(out.guestTypes[1], 1); // first draw 1 % 5
    OBL_CHECK_EQ(out.numGuests, 4);
    OBL_CHECK_EQ(f.env.draws, 1);
}

OBL_TEST_CASE("ferry_arrivals.keyword_list_skips_empty_entries")
{
    Fixture f;
    obl::ferry::Boat b = Boat(2, 0);
    std::strcpy(b.GuestKeyword[1], "Female");
    b.GuestType[1] = 0;
    b.GuestKeywordNumbers[1] = 7;
    f.Add(b);
    obl::ferry::Arrival out{};
    OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK_EQ(out.guestTypes[0], 0);
    OBL_CHECK_EQ(out.guestTypes[1], 7);
    OBL_CHECK_EQ(out.numGuests, 7);
    OBL_CHECK_EQ(f.env.draws, 0);
}

OBL_TEST_CASE("ferry_arrivals.performance_with_zero_happiness_draws_weights")
{
    Fixture f;
    f.Add(Boat(3, 4)); // count 1 (0.01f rounding), intensities 3:1 -> 0 male, 1 female
    obl::ferry::Arrival out{};
    OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK_EQ(out.performance, 1);
    OBL_CHECK_EQ(f.env.draws, 1); // only the female side has a non-zero count
    OBL_CHECK_EQ(f.m.guests.fAvgHapp[1], 0.0f); // the drawn weights are cleared after the boat is built
    OBL_CHECK_EQ(out.guestTypes[0], 0);
    OBL_CHECK_EQ(out.guestTypes[1], 1);
    OBL_CHECK_EQ(out.numGuests, 1);
    OBL_CHECK_EQ(f.m.lastArrived, 1);
}

OBL_TEST_CASE("ferry_arrivals.performance_leavers_total_is_recorded")
{
    Fixture f;
    f.Add(Boat(3, 100)); // rating 50 -> 49 or 50 guests, never zero
    f.m.guests.iHappyLeavers = 3;
    f.m.guests.iUnhappyLeavers = 4;
    f.m.guests.fAvgHapp[0] = 1;
    f.m.guests.fAvgHapp[1] = 1;
    obl::ferry::Arrival out{};
    OBL_CHECK(obl::ferry::PopDueBoat(f.m, f.env, out));
    OBL_CHECK_EQ(f.m.lastLeft, 7);
    OBL_CHECK(out.numGuests >= 45 && out.numGuests <= 50);
    OBL_CHECK_EQ(out.guestTypes[0] + out.guestTypes[1], out.numGuests);
    OBL_CHECK(out.guestTypes[0] > out.guestTypes[1]); // 3:1 male intensity
    OBL_CHECK_EQ(f.env.draws, 0);
}

OBL_TEST_CASE("ferry.record_guest_leaving_halves_the_running_sum")
{
    obl::ferry::Manager m;
    obl::ferry::RecordGuestLeaving(m, 3, 40.0f, false);
    OBL_CHECK_EQ(m.guests.fAvgHapp[3], 20.0f);
    obl::ferry::RecordGuestLeaving(m, 3, 20.0f, true);
    OBL_CHECK_EQ(m.guests.fAvgHapp[3], 20.0f);
    OBL_CHECK_EQ(m.guests.fAvgHapp[0], 0.0f);
    OBL_CHECK_EQ(m.guests.iHappyLeavers, 1u);
    OBL_CHECK_EQ(m.guests.iUnhappyLeavers, 1u);
}
