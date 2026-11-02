# Spec: building events (shows)

## Purpose
A building that hosts shows keeps event slots (a start hour, a duration, the guests and actors booked). This spec covers
when a slot may start and end, which slot a loaded building continues with, and the casts and guest timing.

## Rules
1. A slot starts only strictly after its start hour; it is still active at exactly start plus duration and past its end
   after that. The end is compared in double precision: start 13.0 with duration 0.1 is already past its end at 13.1
   (`building_events.strict_start_and_inclusive_active_end`).
2. After a load, the slot to continue with is chosen from the booked-guest lists, not from stored counters: the first
   slot whose start is not before the clock, wrapping to the first slot after the last start (slots at 9, 13 and 17 give
   slot 1 at 13.0 and slot 0 at 23.0). When two slots share the start hour, the later one wins only while an event is
   active and it has guests booked; otherwise the earlier one is chosen
   (`building_events.loaded_cursor_uses_lists_instead_of_counters`).
3. Casts by show key: weightlifter 3 actors of type 16, singing 1 of 15, catwalk 3 of 17, cabaret 1 of 14, wet T-shirt 4 of
   18, aerobics 1 of 20. Only cabaret and aerobics pick a random variant. An unknown key has no cast
   (`building_events.cast_and_guest_jitter`).
4. A guest's arrival delay is the base value spread by a random number: 0 gives 85 % of the base, the maximum random value
   115 % (`building_events.cast_and_guest_jitter`).

## Not covered by tests
- Booking guests into slots and the effect of a finished show on guests (the game scripts `tests/building_events*.txt`
  replay whole shows; their exact assertions are in those scripts).

## Tests
`tests/unit/building_events_test.cpp`; game scripts `tests/building_events.txt`, `building_events_boundaries.txt`,
`building_events_custom_times.txt`, `building_events_load.txt`.
