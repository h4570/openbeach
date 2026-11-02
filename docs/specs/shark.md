# Spec: sharks

## Purpose
A shark swims inside a rectangle of sea, warns nearby swimmers, and sometimes picks a swimming guest as its victim. Its
behavior is driven by a script; this spec covers the decisions the code makes itself.

## Vocabulary
- **Swim rectangle**: the sea area the shark stays in, set after creation.
- **Swim action**: the shark's choice each time it is idle: bite a victim, or swim to a random point.
- **Quarter**: the swim rectangle is cut in four; the shark's action state (0..3) says which quarter it swims in next.

## Rules
1. The shark knows the states 0, 1 and four script states (0x85 to 0x88); every other state is unknown
   (`shark.has_state_is_zero_one_and_the_four_script_states`).
2. A new shark has no victim, an alert range of 5, a sniff range of 10, its script ("SharkLogic"), no guests eaten, maximum
   force 50 and maximum velocity 100 (`shark.construct_sets_ranges_script_and_speed_and_leaves_the_rest`).
3. Setting the swim rectangle and the owner just stores the values (`shark.set_bounds_and_init_param_are_plain_stores`).
4. Entering a state and leaving one run the script's entry and exit handlers; the per-frame update runs the moving-object
   update first, then the script, and reports a failed moving-object update to the fatal-error path under the shark's name
   (`shark.states_run_the_script_of_the_state`, `shark.update_runs_the_base_then_the_script_and_reports_a_failed_base`).
5. A received message is kept as the current message while the script handles it, and the previous one is put back
   afterwards; a message the script does not take is passed on to the movement part
   (`shark.receive_message_keeps_a_copy_for_the_script_and_puts_the_old_one_back`).
6. Sending a message without a script does nothing; otherwise it is sent now or delayed
   (`shark.send_message_needs_a_script`).
7. Playing an animation picks the animation type from the flags and returns the duration; the name "MessData" means the
   text identified by the data word of the message being received (`shark.play_animation_picks_the_type_and_the_message_text`).
8. Alerting posts a warning message to every swimming guest within the alert range (class guests in the swimming state,
   other states and other classes are skipped), sent by the shark (`shark.alert_guests_warns_every_swimming_guest_in_range`).
9. Swim decision with no victim: the shark looks within the sniff range for swimming guests; with at least one it picks one
   by a random number modulo the count, makes it the victim, tells the guest it is bitten and itself to bite, and the
   action state does not move on (`shark.decide_swim_action_bites_a_swimming_guest`).
10. Otherwise the shark draws two random numbers and swims to a point in the quarter of its action state (0 upper left, 1
    upper right, 2 lower right, 3 lower left), then advances the state modulo 4
    (`shark.decide_swim_action_swims_in_the_quarter_of_the_state`).
11. A set victim is cleared first, with no search that time, and the shark swims. An object that is not a swimming guest is no
    victim. Nobody near costs no random number (`shark.decide_swim_action_clears_the_victim_and_falls_back_to_swimming`).
12. A rectangle with a half-width of zero makes the shark do nothing; a half-height of zero stops it after the first draw
    (`shark.decide_swim_action_with_an_empty_rectangle_stops_instead_of_trapping_it`).
13. The x target is computed through a float, the y target exactly
    (`shark.decide_swim_action_x_goes_through_a_float_and_y_stays_exact`).
14. The two script functions are dispatched by number; script values and inherited arguments are read as for other moving
    objects; direction is clamped only at its upper bound; the save field keys match the save bridge
    (`shark.call_function_dispatches_the_two_script_functions`, `shark.script_values_and_inherited_arguments`,
    `shark.direction_clamps_only_upper_bound_and_text_preserves_unrecognized_buffer`, `shark.field_keys_match_the_save_bridge`).
15. Destroying a shark restores the type's behavior, runs the base destructor, and frees memory only when asked
    (`shark.destruct_restores_the_vtable_then_the_base_then_frees_on_bit_zero`).

## Not covered by tests
- What the script itself does with the states (when it bites, the guest's death) beyond the game scripts `tests/shark_draw.txt`
  and `shark_serial.txt`, which pin drawing and saving.

## Tests
`tests/unit/shark_test.cpp`; game scripts `tests/shark_draw.txt`, `shark_serial.txt`,
`world_create_shark_native.txt`.
