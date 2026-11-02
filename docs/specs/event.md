# Spec: event (disaster) manager

## Purpose
Runs the scheduled events of a scenario (sharks, floods, beer shortages, thunder and strikes on a target): each event has a day, a start
hour and a duration, starts once its time has come, acts while it is active and is cleaned up when it ends. A schedule that
repeats shifts its days forward by a day offset.

## Vocabulary
- **Schedule**: the events in the order they were added; `numEvents` counts them. **Next event**: the index of the next
  event to start. **Active list**: indices of events that are running; `numCurEvents` counts them.
- **Day offset**: how many days a finished schedule moves forward to repeat; **current day offset**: the total shifted so far.

## Rules
1. Adding an event appends it to the schedule when no existing event is later (by day, then by hour); events of equal day
   and hour keep the order they were added in, and the count goes up
   (`event_manager.stable_append_and_middle_insertion_failure`: four events in the order added).
2. Adding an event that is earlier than an existing one would have to go into the middle of the schedule; that is an error
   and leaves the schedule unchanged (same test; the world reports it as an invalid insertion).
3. An event starts only when the clock is strictly past its start (hour 10 of an event at 10 does not start it, 10.01 does),
   and ends when the clock is strictly past start plus duration; a duration that runs past midnight carries into the next
   day (`event_manager.strict_start_end_and_midnight_carry`: a 25-hour flood starts at 10.01 and is still active on the next
   day at 11, ended at 11.01).
4. Event types: 0 sharks, 1 flood, 2 beer shortage, 3 thunder, 4 strike on a named target. A flood (type 1) raises the
   water when it starts and clears it when it ends; thunder (type 3) sets thunder and its area on start and clears the
   thunder at the end (`event_manager.strict_start_end_and_midnight_carry`,
   `event_manager.swap_removal_and_repeat_after_all_finished`, `event_manager.effects_rng_and_non_active_strike`).
5. Ending an event removes it from the active list by moving the last active entry into its place, and that entry is
   checked next; so a flood, a thunder and a flood that end together report flood, flood, thunder
   (`event_manager.swap_removal_and_repeat_after_all_finished`).
6. When every event has been started and none is active and the day offset is not zero, the schedule repeats: the next event
   goes back to 0, the current day offset grows by the day offset and every event's day moves forward by it
   (`event_manager.swap_removal_and_repeat_after_all_finished`: day 0 becomes 2).
7. A shark event places as many sharks as its scale, each at a position drawn with two random numbers inside the event's
   rectangle; at its end it reports "finished" to every shark of the world
   (`event_manager.effects_rng_and_non_active_strike`: scale 2 gives 4 draws and positions 11/22 and 13/24; two sharks
   finish).
8. A beer event gives every building that sells the event's beer the replacement beer and strength on start, marks the beer
   unavailable and announces a shortage; at the end the beer is available again and a restored message is shown
   (`event_manager.effects_rng_and_non_active_strike`).
9. A strike on a named target (type 4) acts once at its start and never joins the active list, so it has no end; the other
   types are active from their start until their end (`event_manager.effects_rng_and_non_active_strike`: four events
   started, three active).
10. A clock that is not a number starts nothing; an event whose duration is not a number ends at the next update
    (`event_manager.x87_unordered_start_and_end`).
11. Constructing the manager sets the world's event flag and zeroes all counters; destroying clears the flag
    (`event_manager.lifecycle construct sets the global and zeroes the counters`).
12. Loading restores the active list from the save, sets the beer availability flags of the saved events, and shifts the
    days of the first `numEvents` events by the saved current day offset (`event_manager.lifecycle load restores active
    indices beer flags and shifted days`: day 8 + offset 5 = 13, then 18 on the second load).

## Not covered by tests
- What the target strike does to the named building, and what the area, strength and water effects look like to the
  player; the tests only record that the effect was requested.

## Tests
`tests/unit/event_manager_test.cpp`, `tests/unit/event_manager_lifecycle_test.cpp`, `tests/unit/building_events_test.cpp`
and `mover_events_test.cpp` for the objects' event messages (not yet written as rules).
