# Spec: staff script calls

## Purpose
Staff characters (builders, cleaners, lifeguards, mechanics, reps, security guards) act through numbered script functions
that their behavior scripts call. Each function either changes the member's own state, forwards to a game service, or
posts a message. This spec lists what each function does, as far as tests pin it. The names below are the numbers the
scripts use, in hex.

## Vocabulary
- **Function id**: the number a script calls. Ids a profession does not handle go to the shared dispatcher (below).
- **Thought**: a bit mask pushed to the member's thought display; **blink**: the member's icon starts or stops blinking.
- **Feedback**: the status a staff member reports to the player; "required feedback" is what is waiting to be shown,
  "current feedback" what was last shown. The **feedback status** is a set of bits; several functions clear one bit (value
  2 or 4), so a status of 7 becomes 5 or 3.
- **Day time**: the game hour; **day length**: the length of a game day in seconds (240 in the tests).

## Shared dispatcher (all staff)
1. Function 0x76 asks for a wander position; 0x79, 0x7a, 0xac and 0xad push the thoughts 0x20000000, 0x40000000,
   0x1000000 and 0x2000000, and 0xad also sets "ended shift"; 0xab directs the member to the object it was sent to
   (`staff_call_function.the_shared_functions_call_the_helpers`).
2. Function 0xa8 spends one unit of idle time per call; at 0 it stays 0, and a negative value keeps counting down
   (`staff_call_function.idle_time_is_spent_down_to_zero`).
3. Function 0xa9 clears the directed flag and the go-to target (-1) (`staff_call_function.the_directed_target_is_cleared`).
4. Function 0xaa turns the sprite to its front view: direction 4 of 8 or 5, 3 of 4, 0 of 1
   (`staff_call_function.the_sprite_faces_front_or_its_last_direction`).
5. Any other id is a fatal error "A script called a function staff members do not have"
   (`staff_call_function.an_unknown_function_is_fatal`: ids 0x75, 0x77, 0x78, 0x7b, 0xa7, 0xae, 0 and -1).

## Common to all professions
6. 0xc makes the member visible, 0xb invisible, and 0xae marks it "waiting for death", for every profession. The
   professions other than the builder also handle: 0xaf starts the icon blinking; 0xb0 moves the required feedback to the
   current one, selects the member, opens tab 2 of its dialog, pushes the thought of that feedback and stops the icon; 0xb1
   moves the required feedback to the current one, looks up its feedback key, adds a feedback entry for the member only
   when a key exists, then pushes the thought and stops the icon (`staff_call_builder.visibility_time_and_forwarded_helpers`,
   `staff_call_cleaner.select_and_feedback`, `staff_call_mechanic.select_and_feedback`, `staff_call_rep.select_and_feedback`,
   `staff_call_security.select_and_feedback_use_the_guard_icon_names`, `staff_call_lifeguard.feedback_functions`,
   `staff_call_lifeguard.selecting_opens_the_tab_then_shows_the_thought`,
   `staff_call_lifeguard.a_feedback_key_is_added_only_when_known`). The icon and dialog names are `Rep`, `Cleaner`,
   `Mechanic`, `Lifeguard`; the security guard uses `SecurityIcon` to start the blink, `SecurityDialog` for the tab and
   `SecurityGuardIcon` to stop it.
7. Feedback entries carry the member's id and a profession number: mechanic 9, rep 10, cleaner 12, security 13, lifeguard 8
   (the tests above); the security guard's detention entry (rule 45) uses 8.
8. Ids a profession does not list are handed to the shared dispatcher (`staff_call_*.unknown_ids_go_to_the_shared_dispatcher`).

## Builder
9. 0xc and 0xb set visibility; 0x5c stamps the time of the last task; 0x5d finds the nearest valid tile; 0x60 checks
   for female guests; 0x62 pushes thought 0x20; 0x64 removes a cost point of the destination job; 0x65 and 0x66 free the
   member's job (the second without the build check); 0x67 and 0x6a reallocate builders; 0xb2 clears delayed messages for
   the member; 0x5e and 0x5f do nothing (`staff_call_builder.visibility_time_and_forwarded_helpers`). 0xaf, 0xb0, 0xb1 and
   0x63 are not builder functions and go to the shared dispatcher (`staff_call_builder.unknown_ids_go_to_the_shared_dispatcher`).
10. 0x61 clears the whistling flag (`staff_call_builder.whistling_is_cleared`).
11. 0x69 clears the required feedback and bit 2 of the feedback status; 0xae waits for death
    (`staff_call_builder.feedback_flags_and_death_wait`).
12. 0x68 posts to itself a delayed message (type 0x39) carrying 99, or 100 when the destination is being demolished; its
    delay is the rest of the hour (day length / 24 minus idle time per hour) scaled down by the random roll, never below 0.
    A missing destination object posts nothing (`staff_call_builder.the_delayed_message_waits_the_rest_of_the_hour`,
    `staff_call_builder.more_idle_time_than_an_hour_clamps_the_delay_to_zero`,
    `staff_call_builder.a_missing_destination_posts_nothing`).

## Cleaner
13. 0x74 stamps the last task time; 0x70 sends the member to the bin position; 0x71 and 0x75 face the destination (the
    second only if present); 0x77 reports "did not clean"; 0x78 reports "started cleaning"; 0x7b pushes thought 0x20000000
    (`staff_call_cleaner.visibility_time_and_forwarded_helpers`).
14. 0x6e looks for an urgent job; only when one is found it becomes the destination and is remembered as urgent with its type
    (`staff_call_cleaner.an_urgent_job_is_remembered_only_when_found`).
15. 0x6f cancels abandoned litter: the destination gets a message (type 3, data 3) and is marked "waiting for death", its
    moving and called flags are cleared, thought 0x40 is pushed and the litter count of the day goes up; if the litter is
    already gone only the thought and the count remain (`staff_call_cleaner.abandoned_litter_is_cancelled_and_marked_for_removal`).
16. 0x72 releases an emptied bin (claim, moving and called cleared, fill 0, thought 8, job cancelled) and counts it for
    the day (`staff_call_cleaner.an_emptied_bin_is_released`).
17. 0x73 finishes a toilet: the toilet's cleaned-today count goes up and its visits since cleaning reset; the toilet is
    released (claim, moving and called cleared), thought 0x10 is pushed, the job is cancelled and the member's toilets of the
    day go up. From the third cleaning of the day the member is flagged "busy toilet" with the toilet's id, and its required
    feedback and bit 2 of the feedback status clear (`staff_call_cleaner.a_toilet_busy_flag_is_set_from_the_third_cleaning`).
18. 0x7c posts to itself a delayed message (type 0x39, carrying 0x73) whose delay is half the toilet's visits since
    cleaning, between 2 and 15: 2 for 0-5 visits, 3 for 6, 10 for 20, 15 for 30 or more
    (`staff_call_cleaner.the_cleaning_delay_follows_the_visits_clamped_to_two_to_fifteen`).
19. 0x7d searches for any job: no job clears the three found flags and the destination (-1); a job of type 0, 1 or 2 sets
    the litter, bin or toilet flag and stamps the task time; an unknown type keeps the job and sets no flag
    (`staff_call_cleaner.the_job_search_sets_one_found_flag_by_type`).
20. 0xad on a cleaner clears the required feedback and bit 4 of the feedback status, then goes to the shared dispatcher
    (`staff_call_cleaner.select_and_feedback`).

## Lifeguard
21. 0x2f heads for the shore; 0x89 moves to the tower; 0x8a finds a beach point; 0x8b moves to the sea entry; 0x8f and 0x90
    set the draw position for build and serve; 0x91 finds the beach; 0x98 looks up the rescued guest; 0x7b pushes thought
    0x20000000; 0x8c does nothing (`staff_call_lifeguard.visibility_and_simple_thoughts`,
    `staff_call_lifeguard.movement_helpers_are_forwarded`).
22. 0x88 posts to itself a message (type 9) carrying its hotel's id (`staff_call_lifeguard.a_message_to_self_carries_the_hotel_id`).
23. 0x8d puts the rescued guest at the lifeguard's position and gives it the lifeguard's direction; 0x8e turns the
    lifeguard to 7 minus twice the tower's direction (3 for tower direction 2, -1 for 4); 0x96 hides and 0x97 shows the
    rescued guest (`staff_call_lifeguard.the_rescued_guest_is_moved_and_turned`).
24. 0x92 records a drowned guest (drowned flag, drowned count, times called out; clears the required feedback and bit 2 of
    the feedback status); 0x93 counts a save and a call-out and pushes thought 0x80 (`staff_call_lifeguard.drowning_and_saving_counters`).
25. 0x94 sets "in my area" when the sea area of the guest equals the lifeguard's influence area
    (`staff_call_lifeguard.the_guest_is_in_my_area_when_the_sea_area_matches`).
26. 0x95 poses the rescue: both lifeguard and guest are placed by offsets that depend on the lifeguard's direction, both
    computed from the position before the move (direction 2 from (100, 200): lifeguard (177, 188), guest (167, 204)); the
    lifeguard's direction becomes (direction + 2) % 8 and the guest's (guest direction + 6) % 8, each clamped to the last
    direction its sprite has. A lifeguard direction of 8 or more moves nobody, but the directions still turn
    (`staff_call_lifeguard.the_rescue_pose_uses_the_offsets_of_the_current_direction`,
    `staff_call_lifeguard.an_out_of_range_direction_keeps_the_position_and_is_clamped`).
27. 0xad clears the required feedback and bit 4 of the feedback status, then the shared dispatcher runs
    (`staff_call_lifeguard.feedback_functions`).

## Mechanic
28. 0x74 stamps the task time, 0x7b pushes thought 0x20000000 (`staff_call_mechanic.visibility_time_and_thought`).
29. 0x99 looks for work according to the job mask bits (1 repairs, 2 vandalism, 4 watering), starting with repair type 2
    and no destination. With the watering bit and a roll (`random % 100`) above 80 it asks for kind 3 first; unless that
    lookup reports type 3 it goes on: with bits 1 and 2 a second roll asks for kind 1 first when below 51, else kind 2
    first, then the other; with bit 1 only, kind 2; with bit 2 only, kind 1; then with bit 1, kind 0 when nothing was
    found. A job found becomes the destination, sets "found repair",
    stamps the task time and stores the repair type reported by the lookup; with none found the destination is -1, the flag
    is cleared and the task time is not touched (`staff_call_mechanic.a_repair_only_mechanic_looks_for_repairs_then_inspections`,
    `staff_call_mechanic.a_vandal_only_mechanic_asks_for_kind_one`,
    `staff_call_mechanic.a_mixed_mask_picks_the_first_kind_by_the_roll_and_falls_back`,
    `staff_call_mechanic.nothing_found_clears_the_flag_and_skips_the_timestamp`,
    `staff_call_mechanic.a_watering_roll_above_eighty_tries_kind_three_first`).
30. 0x9a looks for a better job: a result replaces the destination and sets "found better job"; none keeps the destination
    (`staff_call_mechanic.a_better_job_replaces_the_destination`).
31. 0x9b looks for a watering job only when the mask has the watering bit; a job becomes the destination with repair type 3,
    "found repair" set and the task time stamped, none sets the destination to -1; without the bit the destination stays
    and "found repair" clears (`staff_call_mechanic.watering_needs_the_watering_bit_and_a_job`).
32. 0x9d returns the job: kind 2 for a building, 1 for scenery (`staff_call_mechanic.returning_a_job_depends_on_the_object_type`).
33. 0x9e finishes a job: it pushes the thought of its repair type (0: 4, 1: 0x10, 2: 8, 3: 0x20), releases the destination and
    clears it; an unknown type releases -1 (`staff_call_mechanic.finishing_a_job_pushes_the_thought_for_its_type_and_releases_it`).
34. 0x9f reports "started" for the destination and counts the day's inspections (type 0), vandalism jobs (1), repairs (2) or
    waterings (3); other types count nothing (`staff_call_mechanic.starting_a_job_bumps_the_daily_counter_of_its_type`).
35. 0xa0 stores the watering position as the target's position plus (34, -22); 0xa1 turns the mechanic to direction 5, or the
    last direction if the sprite has fewer (`staff_call_mechanic.the_watering_position_is_offset_from_the_target`,
    `staff_call_mechanic.the_watering_pose_faces_direction_five_or_the_last_one`).
36. 0xad on a mechanic clears the required feedback and bit 2 of the feedback status, then goes to the shared dispatcher
    (`staff_call_mechanic.select_and_feedback`).

## Rep
37. 0x7b pushes thought 0x20000000; 0xa2 moves to the next building; 0xa3 looks for guests; 0xa4 reports arrival at a
    guest; 0xa6 pushes thought 0x20000 (`staff_call_rep.forwarded_helpers_and_visibility`).
38. 0xa5 counts one more guest talked to and adds the talk time (scaled by 8, at least 1, rounded down) to the happiness
    boost of the day: 5 gives 40, then 0 and -3 give 1 each (`staff_call_rep.talk_time_is_scaled_and_accumulated_in_a_float`).
39. 0xa7 handles an opinion message: it counts the opinion by topic and kind; for any kind but the neutral one (2) it also
    clears the required feedback, sets the feedback status to the topic, stores the kind as the opinion value and updates
    the feedback; a neutral opinion only counts
    (`staff_call_rep.an_opinion_message_updates_the_counters_and_feedback`,
    `staff_call_rep.a_neutral_opinion_only_counts`).

## Security guard
40. 0xc and 0xb set visibility; 0x7b pushes thought 0x20000000 (`staff_call_security.visibility_and_thought`).
41. 0x7e stores whether an offender was found (`staff_call_security.the_offender_search_result_is_stored`).
42. 0x7f collects a serious fine and counts it, 0x80 a minor fine and counts it (each asks the game to collect a fine of that
    kind); the fine total of the day is the sum of the amounts (one serious 50 and two minor 5 give 60) (`staff_call_security.fines_accumulate_per_kind`).
43. 0x81 counts a jailed offender, 0x82 a cautioned one (`staff_call_security.jailed_and_cautioned_counters`).
44. 0x83 sets the fine offset to the home building's serve point plus 22 on the x axis, same y
    (`staff_call_security.the_fine_position_is_offset_from_the_serve_point`).
45. 0x85 faces the offender, 0x86 chooses an action, 0x87 adds the feedback "security guard has detained"
    (`staff_call_security.offender_helpers_and_detention_feedback`).

## Not covered by tests
- What the individual thought bits look like on screen, and which scripts call which function.
- The numeric values of the lifeguard rescue offsets other than the direction-2 and out-of-range cases.
- Whether the ids a profession passes to the shared dispatcher change behavior beyond the dispatcher's own tests.
- 0x99 with both the watering bit and other bits when the watering lookup finds nothing (the code goes on to the other
  kinds; the tests run the watering bit alone).

## Tests
`tests/unit/staff_call_function_test.cpp`, `staff_call_builder_test.cpp`, `staff_call_cleaner_test.cpp`,
`staff_call_lifeguard_test.cpp`, `staff_call_mechanic_test.cpp`, `staff_call_rep_test.cpp`,
`staff_call_security_test.cpp` (shared fake environment in `staff_call_fixture.h`).
