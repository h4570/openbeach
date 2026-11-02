# Spec: building update

## Purpose
Coordinate each building's work, wear, event schedule, service sounds and waiting demolition.

## Rules
1. Working states 0 and 4 run the working head, wear, working tail, then sound-flag update, in that order. Startup
   updates the construction hoarding; other tested states only update the sound flag
   (`building_update.working_states_run_head_wear_tail_and_the_sound_flag_in_order`,
   `building_update.startup_ticks_the_hoarding`, `building_update.other_states_only_run_the_sound_flag`).
2. The working head computes today's profit as income minus cost and closes the dialog only for selection level 1
   (`building_update_tail.head_recomputes_the_profit_and_closes_the_dialog_of_a_selected_building`,
   `building_update_tail.head_closes_the_dialog_only_for_select_level_one`).
3. The tail runs events only if a schedule exists, before updating the opening state
   (`building_update_tail.events_run_only_with_a_schedule_and_before_the_open_state`).
4. A toilet requests cleaning after more than 25 visits, provided cleaning is not already called, construction and
   demolition are inactive, and the job is unclaimed. Checks stop at the first failing condition; other building names
   do not request toilet cleaning (`building_update_tail.toilet_asks_for_a_cleaner_after_more_than_25_visits`,
   `building_update_tail.toilet_cleaning_needs_every_condition`,
   `building_update_tail.toilet_checks_stop_at_the_first_failing_one`,
   `building_update_tail.other_buildings_with_many_visits_are_not_cleaned`).
5. The toilet fly loop starts above 20 visits and stops otherwise. Toilet name matching is whole-name matching;
   non-toilet buildings with their sound-playing flag set request their sound again every working update
   (`building_update_tail.flies_start_above_20_visits_and_the_loop_stops_otherwise`,
   `building_update_tail.names_are_compared_whole`,
   `building_update_tail.a_building_with_its_sound_playing_asks_for_it_again_every_update`).
6. Jet ski and speedboat hire buildings run the idle loop while open with at least one counted free vehicle. Closed,
   empty or fully busy hire buildings stop it; other buildings do not use this hire loop
   (`building_update_tail.hire_buildings_idle_while_open_with_a_free_vehicle`,
   `building_update_tail.hire_buildings_stop_when_closed_busy_or_empty`,
   `building_update_tail.hire_loop_ignores_other_buildings`).
7. Opening starts silent building sound and then sets the playing flag; closing stops playing sound and then clears
   the flag. Already matching states do nothing; open-status values other than 0 and 1 leave the flag alone
   (`building_update_tail.sound_flag_follows_the_opening`,
   `building_update_tail.sound_flag_ignores_other_open_statuses`).
8. Waiting demolition closes the building, cancels an active scheduled show, updates the hoarding, then waits for
   vehicles, occupied chairs, active loungers, capacity and guests to clear. Without a schedule it does not ask about
   shows; an idle show is not cancelled (`building_update.waiting_demolition_closes_ticks_and_starts_when_empty`,
   `building_update.demolition_waits_for_everything_to_clear`,
   `building_update.demolition_does_not_cancel_an_idle_show_or_ask_without_a_schedule`).
9. Once empty, demolition marks owned vehicles, tables, loungers and service staff for removal, sends loungers their
   state change, clears the vehicle/table/lounger counts and starts demolition. Staff of other classes stay. Missing
   objects are skipped, but lounger messages still go to their identities
   (`building_update.waiting_demolition_closes_ticks_and_starts_when_empty`,
   `building_update.demolition_skips_missing_objects_and_checks_the_staff_class`).
10. Already started demolition skips this waiting cleanup and only updates the sound flag
    (`building_update.demolition_in_progress_does_nothing_but_the_sound_flag`).

## Edge cases
- Only vehicle-in-use value 1 blocks demolition. Negative capacity does not block, but a negative guest count does
  (`building_update.demolition_vehicle_flag_must_be_exactly_one_to_block`,
  `building_update.demolition_capacity_test_is_signed_and_guests_unsigned`).
- A chair occupier of zero is occupied; no chairs is empty
  (`building_update.chairs_are_empty_when_no_seat_has_an_occupier`).
- Negative visits count as a large unsigned count for cleaning and flies. NaN construction/demolition query values
  do not block cleaning (`building_update_tail.negative_visits_count_as_a_huge_unsigned_number`,
  `building_update_tail.toilet_cleaning_needs_every_condition`).

## Not covered by tests
Sound tests record loop and start/stop requests; they do not judge audibility or continuity of the resulting audio.

## Tests
`tests/unit/building_update_test.cpp`, `tests/unit/building_update_tail_test.cpp`.
`tests/building_update.txt` asserts construction and demolition state/count changes and live branch call counts.
Wear decisions are specified in [building-breakdown.md](building-breakdown.md).
