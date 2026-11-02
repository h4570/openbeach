# Spec: scenery (benches, loungers, bins, lamps, fires)

## Purpose
The small decisions scenery objects make: which states exist, selection, seats, bin fill, repair steps, lamp hours, refunds
and placement bookkeeping. Whole-object behavior (drawing, placing, states) is pinned by the game scripts listed below.

## Rules
1. Scenery knows the states 0, 1, 3 and 4 (`scenery.has_state_is_0_1_3_4`). Only states 1 and 3 do something on entry
   (`scenery_logic.enter_state_and_lounger_direction`).
2. Scenery is selectable when it has a selection flag or a second flag and is not burning
   (`scenery.is_selectable_needs_a_flag_and_no_burn`).
3. Free seats are seats minus taken seats, with integer wraparound (`scenery.free_seat_count_wraps`). A seat message sender
   counts unless it is the object itself or one of the two system senders (`scenery_logic.seat_sender_excludes_self_and_system`).
   Finding a seat returns the first seat holding the wanted occupant, or the seat count when none does
   (`scenery_logic.find_seat_first_match_or_max`). Guest states 24, 29, 51 and 53 are seated
   (`scenery_logic.seated_guest_states`).
4. A bin's fill is clamped: up to 70 % of its capacity it is kept, above that it becomes the full capacity; the bin
   animation is shown only for bins, and "full" means exactly full (`scenery.bin_fill_clamps_above_seventy_percent`,
   `scenery.bin_animation_only_for_bins_and_full_means_exactly_full`, `scenery_construction.bin_animation_follows_the_capacity`).
5. Repair state steps down one level at a time to 0; the reply type to a repair request depends on the state (92 for 0, 91
   for 2) and the nuisance level is 2 when asked for and 1 otherwise (`scenery_logic.repair_state_steps_down`).
6. The repair update chooses between nothing, calling a mechanic and flickering from five conditions; the flicker light is
   on at phases 0 and 40 and off at 19 (`scenery_logic.repair_update`; the meaning of each condition is not named in the
   test, see below).
7. Litter jitter spreads over -30..29 in x (period 60) and -14..13 in y (period 28) (`scenery_logic.litter_jitter_and_snap`).
8. Selling returns the cost plus a twenty-percent share computed in double precision, so float error shows in the result
   (`scenery_logic.sell_refund_is_twenty_percent_through_double`).
9. Construction: a plain object gets its defaults; a bench has two seats and a menu; a street lamp draws its on and off hours
   from the random generator in order (its key is case sensitive, the others are not); a beach fire has fixed hours and
   draws no random number; destroying a lamp releases its light sprite (`scenery_construction.plain_object`,
   `scenery_construction.bench_has_two_seats_and_a_menu`, `scenery_construction.street_lamp_draws_hours_in_order`,
   `scenery_construction.streetlamp_key_is_case_sensitive_but_the_others_are_not`,
   `scenery_construction.beach_fire_has_fixed_hours_and_no_random_draw`, `scenery_construction.destruct_releases_the_light_sprite`).
10. Loading a footprint joins every tile of it except the first (the object's own) to the map; an empty footprint joins
    none (`scenery_record.load_joins_the_footprint_except_the_first_tile`, `scenery_record.load_of_an_empty_footprint_joins_nothing`).
    The draw routine is chosen by sprite type, then name, then burn state (`scenery_record.draw_routine_is_picked_by_sprite_type_then_name_then_burn`).
11. Moving between map cells moves the object's list entry; inside one cell the lists stay; an object not yet listed joins
    the new cell (`scenery_placement.set_position_moves_between_cells`, `scenery_placement.set_position_in_the_same_cell_keeps_the_lists`,
    `scenery_placement.set_position_of_an_unlisted_object_joins_the_new_cell`).
12. Removing plain scenery only posts the removal; removing burned scenery restores the terrain under it and rebuilds it
    (`scenery_placement.removal_of_plain_scenery_only_posts`, `scenery_placement.removal_of_burned_scenery_restores_and_rebakes`).
13. Selection flags, the status texts and the vandalism reaction (message, animation, repeat) are pinned in
    `scenery_actions.selection_flags_order_and_zoom`, `scenery_actions.text_statuses_flags_and_unrecognized_key`,
    `scenery_actions.vandalism_message_animation_and_repeat`. The bench seat offsets follow the seat and direction
    (`scenery.bench_offsets_follow_seat_and_direction`). The save field keys match the converter
    (`scenery_record.field_table_keys_match_the_converter`).

## Not covered by tests
- Which condition of the repair update means what; the test only fixes the combinations.
- The exact lounger direction table beyond the cases in `scenery_logic.enter_state_and_lounger_direction`.
- Cell flag bits and their meaning for pathing (only the set and clear arithmetic is asserted,
  `scenery_logic.cell_flag_and_dirty_bit`).

## Tests
`tests/unit/scenery_*_test.cpp`, `tests/unit/prop_test.cpp`; game scripts `tests/scenery_*.txt`,
`command_place_scenery.txt`.
