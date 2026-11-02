# Spec: ferry manager

## Purpose
The ferry brings guests to the island on a schedule, and a performance (a staged show) can call an extra boat whose crowd
depends on the rating and on how happy the previous visitors were.

## Vocabulary
- **Boat**: a day, an hour, a flag word, a guest-info kind, a guest count and, for keyword boats, up to one keyword per guest
  type. The flag word: 0 = once, on its day; 1 = daily; 2 = daily from its day on; 3 = a performance request.
- **Schedule**: the boats of flags 0-2 in hour order. The **cursor** names the next boat to arrive; it is empty until the
  first schedule insert or a load, and "past the end" once every boat of the schedule has been served.
- **Pending performances**: boats of flag 3, queued at the current hour and day.
- **Guest info**: 0 = random count, 1 = fixed count, 2 = keyword list, 3 = performance crowd.

## Rules
1. A boat of flags 0-2 is inserted before the first boat whose hour is not earlier than its own, so equal hours put the
   newer boat first. A boat of flag 3 goes to the pending performances, stamped with the current hour and day
   (`ferry.order_cursor_and_pending_guard`).
2. While the cursor is past the end of the schedule, no boat is delivered, not even a pending performance; asking
   returns nothing. As soon as an insert lands at the head of the schedule the cursor goes back to the head
   (`ferry.order_cursor_and_pending_guard`).
3. A pending performance is delivered before the schedule, once the hour has reached its time
   (`ferry.order_cursor_and_pending_guard`). With no cursor at all, it is still delivered, the performance list empties and
   the cursor stays empty (`ferry.null_cursor_pops_a_pending_performance`).
4. Inserting a boat before the cursor's boat leaves the cursor on its boat
   (`ferry.insert_before_cursor_keeps_the_cursor_on_its_boat`).
5. A boat at the cursor is delivered only when the hour has reached its time. A from-day boat (2) whose day has not come
   yet is skipped: asking returns nothing and the cursor moves on to the next boat
   (`ferry.order_cursor_and_pending_guard`: the day-2 boat, then the 8:00 daily boat).
6. A delivered once-boat is removed from the schedule and the cursor moves to the next boat
   (`ferry.one_shot_at_cursor_is_removed_and_the_cursor_moves_on`, `ferry.one_shot_removal_and_performance_rounding`). A
   flag-3 boat found at the cursor is removed too and the cursor moves on
   (`ferry.flag_three_boat_at_the_cursor_moves_the_cursor_on`).
7. Random-count boats (guest info 0) bring `random % count` guests, at least 1; a count of 0 brings 1 guest and draws
   nothing for it (`ferry_arrivals.random_count_uses_rand_modulo`, `ferry_arrivals.random_count_never_below_one`). Each guest
   then draws its own type as `random % number of guest types`. Fixed-count boats (1) draw only the types
   (`ferry.order_cursor_and_pending_guard`: 4 guests, types sum to 4).
8. Keyword boats (guest info 2) go through the keyword slots, skip empty slots, and add per guest type either the stated
   number (kind 0) or `random % number` (other kinds). The guest total is the sum, replacing any earlier value in the output
   (`ferry_arrivals.keyword_list_fixed_and_random_numbers`, `ferry_arrivals.keyword_list_skips_empty_entries`). Slots
   beyond the number of guest types are never read.
9. Performance boats (guest info 3) size the crowd as the rating percentage times the boat's count, truncated. The male
   share is the male intensity's part of the crowd, truncated, and the rest is female; inside a gender the crowd is split by
   the recent leavers' happiness averages, each type's number truncated (so the total can fall short of the crowd). When a
   gender has a crowd but all its averages are zero, a random weight is drawn per type of that gender; no draw happens for
   a gender with no crowd. The weights are cleared afterwards
   (`ferry_arrivals.performance_with_zero_happiness_draws_weights`,
   `ferry_arrivals.performance_leavers_total_is_recorded`). Float rounding can cost one guest: a rating of 50 on a boat
   of count 4 brings 1 guest (`ferry.one_shot_removal_and_performance_rounding`).
10. A performance boat stores how many guests left since the last performance (happy plus unhappy) and how many arrive
    (`ferry.one_shot_removal_and_performance_rounding`: 4 and 1, `ferry_arrivals.performance_leavers_total_is_recorded`:
    7). After every delivered boat the happiness averages are reset (`ferry.one_shot_removal_and_performance_rounding`).
11. A guest leaving updates the running value of its type to the mean of the old value and the new happiness, and counts as
    a happy or unhappy leaver (`ferry.record_guest_leaving_halves_the_running_sum`).
12. The next-boat query returns the hour of the boat that comes soonest after "now" and sets its day; a daily boat whose hour
    has passed or is exactly now counts for tomorrow; with "today only" a boat on a later day gives -1; an empty schedule gives -1 and leaves
    the day output alone (`ferry.time_queries_exact_hour_and_future_day`).
13. The "boat time today" query lists, in schedule order, the boats that run on a day (once-boats of that day, daily
    boats, from-day boats started by then) and returns the hour of the one at the given index, -1 past the last; the
    maximum day value lists every boat (`ferry.time_queries_exact_hour_and_future_day`).

## Edge cases
- An hour that is not a number delivers nothing, writes nothing to the output and draws no random number; a boat whose
  hour is not a number sorts behind the others (`ferry.x87_unordered_schedule_and_no_write_due_check`; the name is
  historical).
- A saved cursor index past the end wraps around the ring of boats when loaded: with three boats, index 3 is "past the
  end", 4 is the first boat again and 5 the second; the "no cursor" value loads as no cursor
  (`ferry.lifecycle cursor index past the end wraps around the schedule ring`).

## Not covered by tests
- That daily and from-day boats stay in the schedule after delivery (they do in the code; no test asserts it).
- That a once-boat (0) whose day has not come is skipped like a from-day boat, and that daily boats are never skipped.
- That a performance crowd of 0 becomes 2 (every test runs at rating 50 and gets at least 1).
- That the happy and unhappy leaver counts are reset after every delivered boat (the code does; the tests check only the
  averages).
- `tests/ferry.txt` only checks that the schedule calls are made in a running game; it pins no rule.

## Tests
`tests/unit/ferry_test.cpp`, `tests/unit/ferry_lifecycle_test.cpp`, game script `tests/ferry.txt`.
