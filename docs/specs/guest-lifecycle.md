# Spec: guest sleep, departure and attraction counting

## Purpose
How a guest decides it is time to go to bed, whether it leaves the resort, when it leaves on its last day, and which
attraction counter it belongs to. These are the pure decisions; the thoughts, messages and walking that follow them are
not part of this spec.

## Vocabulary
- **Sleep window**: the guest's wake hour and bed hour. The window **wraps** when the bed hour is earlier than the wake
  hour (wake 9, bed 1).
- **Nights left**: how many nights the guest still wants to stay. **Leave time**: the hour on the last day after which the
  guest wants to go. **Souvenir flag**: set once the guest has decided to buy souvenirs before leaving.

## Rules
1. With a window that does not wrap (wake 7, bed 23), it is bed time from the bed hour onward on any day, and in the small
   hours before the wake hour on every day except day 0 (nobody was in bed the night before the first day). At midday it is
   never bed time (`guest_lifecycle.bed_time_windows`).
2. With a wrapping window (wake 9, bed 1), it is bed time between the bed hour and the wake hour (hour 3), but not before the
   bed hour (hour 0.5), on days after day 0 (`guest_lifecycle.bed_time_windows`).
3. A window whose bed and wake hours are equal is never bed time (`guest_lifecycle.bed_time_windows`).
4. Over every combination of 12 sample values (including "not a number") for wake, bed and hour and the days 0, 1 and 5,
   the answer equals that of the expected-value helper of the same comparisons in the test, and more than 100 of those
   combinations are bed time (`guest_lifecycle.bed_time_matches_the_expected_answer_incl_nan`).
5. The hour a guest leaves on its last day is the wake hour plus one hour plus up to two more, spread by a random draw in
   0..32767: draw 0 with wake 7 gives 8, draw 16384 with wake 9 gives about 11
   (`guest_lifecycle.time_to_leave`).
6. The leave decision, in priority order (`guest_lifecycle.leave_decision`):
   1. **Unhappy**: happiness of 0.25 or less (a happiness that is not a number counts too) leaves at once, whatever else is
      set.
   2. **Go home**: otherwise, when the souvenir flag is set.
   3. **Souvenirs**: otherwise, when no nights are left and the leave time is earlier than the current hour, the guest
      decides to buy souvenirs (the flag is set by the caller).
   4. **Stay**: in every other case, including nights remaining or the leave time not yet reached.
   A leave time or hour that is not a number counts as "leave time has passed".
7. A guest counts in one of six attraction slots: its attractiveness band plus three per gender value (gender 1 adds three). Bands: below 0.2
   (or not a number) = 0, up to 0.7 = 1, above 0.7 = 2. 0.2 itself is in band 1, 0.7 in band 1, 0.71 in band 2
   (`guest_lifecycle.attraction_slot_bands_and_gender`).

## Edge cases
- The limits are compared as the decimal values 0.2 and 0.7 against the stored single-precision number, so the stored 0.2
  is just above its limit (band 1) and the stored 0.7 just below its limit (band 1), as the test records.

## Not covered by tests
- Which gender value means what (the test only uses 0 and 1).
- What the guest does after each decision: the thoughts, messages and the order in which bed time and leaving are checked.
- Where the sleep window, the nights left and the happiness come from.

## Tests
`tests/unit/guest_lifecycle_test.cpp`.
