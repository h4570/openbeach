# Spec: building open state

## Purpose
Each frame a building decides whether to open, close, or stay as it is, from the opening hours, the clock hour, the repair
state, forced closures and the power situation. Also the power a building draws or delivers.

## Vocabulary
- **Opening hours**: a start and an end hour (fractions allowed). Equal ends mean round the clock; an end before the start
  wraps midnight.
- **Whole hour**: the clock hour rounded down (floor, so -0.5 gives -1).
- **Forced closure**: closed by the player or shed by the power grid.

## Rules
1. The whole hour is the floor of the value (`building_open_state.whole_hour_is_floor_then_truncate`).
2. "Closed during the hours" needs a forced closure; an unforced building is never in that state
   (`building_open_state.closed_during_hours_needs_a_forced_closure`).
3. A plain window includes both ends (8..20 matches 8.0 and 20.0, not 7.99 or 20.01). A wrapping window (20..6) is
   everything except the gap strictly between the ends. Equal ends match every time
   (`building_open_state.closed_during_hours_window_includes_both_ends`,
   `building_open_state.closed_during_hours_wrapping_and_round_the_clock`).
4. A consumer's output is its power value, also when closed. A generator (negative value) delivers nothing while closed.
   An open generator delivers the absolute value, reduced by wear: wear 1 gives 75 %, 2 gives 50 %, 3 gives 25 % (cut
   to a whole number); any other wear value gives the full output
   (`building_open_state.power_output_of_a_consumer_and_a_closed_generator`,
   `building_open_state.generator_output_falls_with_the_wear`).
5. A building shed for power comes back only when the available power is strictly above its own power plus demand. While
   it is shed, the opening hours are not looked at
   (`building_open_state.power_closed_building_returns_when_output_covers_it`).
6. A building closed by the player does nothing. A broken building (repair state 3) does nothing; a merely damaged one
   (state 2) follows the clock (`building_open_state.player_closure_and_a_broken_building_are_left_alone`).
7. Plain hours: inside the window the answer is "open" every frame, even when already open; outside it the answer is "close"
   only for an open building, otherwise nothing (`building_open_state.plain_hours_open_every_frame_and_close_once`).
8. Wrapping hours are open in the evening and the early morning and closed in between, with the same open/close rule
   (`building_open_state.wrapping_hours_open_in_the_evening_and_the_morning`).
9. Round the clock (equal ends, also non-zero) always asks to open
   (`building_open_state.round_the_clock_always_opens`).
10. Fractional ends are compared with the whole hour: with 9.5..17.5, hour 9 is outside and hour 17 is inside
    (`building_open_state.fractional_ends_compare_against_the_whole_hour`).

## Edge cases
- A value far outside the integer range for the whole hour gives the smallest integer (rule 1 test).

## Not covered by tests
- How the result is applied (messages, guests leaving) and when the decision is asked.

## Tests
`tests/unit/building_open_state_test.cpp`.
