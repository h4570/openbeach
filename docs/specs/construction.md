# Spec: construction manager

## Purpose
Keeps one job per building under construction or demolition, a table of builders, and the progress of the work. Builders
are spread over the open jobs; the work of each job ends when its remaining work reaches zero.

## Vocabulary
- **Job**: building id, a "construction" flag and a "demolition" flag (neither = a plain job that counts as a demolition
  for progress), a started byte, a finished flag, remaining work and progress for both kinds, a builder count and ten
  work-point slots whose owner is a builder token (-1 = free).
- **Builder entry**: builder id, status (0 = usable for assignment, 1 = not), assigned building id (-1 = none), a
  "manually assigned" flag and a location. The table holds at most 50 entries; only entries below the table count are
  registered builders.
- **Open-job counter**: the manager's count of construction and demolition work in progress. Starting work raises it and
  the end of the work lowers it; deleting a job does not touch it.
- **Mode** of a search: 0 = construction jobs, 1 = demolition jobs, 2 = all jobs (3 and above match nothing).
- **Capacity** of a job: the building's maximum builders; a job has room while its builder count is below it.

## Rules
### Jobs and queries
1. Finding a job by building id returns the first job of that id (`construction.find_job_returns_the_first_match`). Jobs are
   appended at the end of the list; deleting removes the first match, reports false when none matches, and does not touch
   the open-job counter (`construction.append_job_links_at_the_end`, `construction.delete_job_removes_the_first_match`).
2. Counting by mode: 0 counts jobs with the construction flag, 1 those with the demolition flag, 2 all jobs, other modes 0
   (`construction.count_jobs_by_mode`).
3. Build and demolition progress read 1.0 (finished) when the building has no job; stored progress is clamped to
   0..1, a value that is not a number reads 0 and -0 passes unchanged (`construction.clamp_progress_treats_nan_as_below_zero`,
   `construction.progress_defaults_to_finished_without_a_job`).
4. "Is building" and "is demolishing" answer 0 when the job's object no longer exists or the building has no job, otherwise
   the stored flag byte as a number (255 stays 255) (`construction.is_building_and_demolishing_need_the_object`). "Is
   started" returns the stored byte, 0 without a job (`construction.is_started_returns_the_raw_byte`); setting it writes only
   the first job of the building and reports whether a job exists (`construction.set_started_writes_the_first_jobs_byte`).
5. A new job gets all progress and flags cleared, its fence list emptied, remaining work equal to the building's build and
   demolish times (a negative time reads as a large unsigned number), and ten free work points at (0, 0) when the building
   has a footprint, none otherwise (`construction.construct_job_with_a_footprint_has_ten_free_points`,
   `construction.construct_job_without_a_footprint_has_no_work_points`).
6. A builder's work direction is the direction of the work point it owns minus two steps per rotation step of the building,
   wrapped into 0..7; a builder that owns no point uses the first slot. With no job or no work points it is 0
   (`construction.work_direction_turns_by_the_building_rotation`).
7. A builder's location is readable and settable only for entries inside the table count (-1 is read for an unregistered
   builder); for repeated ids the first entry wins (`construction.builder_location_and_usable_builders`,
   `construction.set_builder_location_only_inside_the_count`). The usable-builder count counts the registered builders who
   can work now (on shift at an open home building) (`construction.builder_location_and_usable_builders`).

### Builder table
8. Adding a builder appends a clean entry (status 0, location 0, no building, not manual) and asks for a reallocation; a
   full table refuses without reallocating (`construction.add_builder_appends_a_clean_entry`,
   `construction.add_builder_refuses_a_full_table`).
9. Removing a builder gives its job back (job's builder count down, the first work point owned by the builder's token
   freed), clears the assignment and manual flag of every entry with its id, closes the gap in the table by moving the later
   entries up, and asks for a reallocation (`construction.remove_builder_gives_the_job_back_and_closes_the_gap`). Without a
   builder object the work-point token is kept; an empty table refuses
   (`construction.remove_builder_without_an_object_keeps_the_tokens_and_without_entries_refuses`).
10. Freeing a builder from its job does the same job hand-back as rule 9 but keeps the table entry; a table count outside
    0..50 is refused, an empty table does nothing (`construction.free_builder_of_job_gives_the_job_back_and_keeps_the_entry`,
    `construction.free_builder_of_job_without_an_object_keeps_tokens_and_a_bad_count_refuses`).
11. Clearing a builder's assignment without telling the job sets every entry with its id to no building and not manual;
    the job keeps its builder count and the work-point token, the location stays
    (`construction.free_builder_no_build_call_check_clears_only_the_entries`).

### Assigning builders
12. A job has room when its builder count is below its capacity; a capacity that is not a number leaves room, a capacity of
    0 or below leaves none, and a negative builder count is read as a huge unsigned number, so such a job has no room
    (`construction.job_has_room_compares_like_the_x87_code`).
13. Assigning a builder to the nearest job picks, among jobs of the mode that have a building and room, the one with the
    smallest tile distance (sum of the two axis differences) from the position; ties keep the first in the list. It marks the
    builder entry (assigned building, manual flag cleared), raises the job's builder count and returns the building id,
    with an output flag of 0 for a construction job and 1 otherwise; with no match, or for a builder without an entry, it
    returns -1 and writes nothing (`construction.assign_nearest_picks_the_closest_tile_and_marks_the_builder`,
    `construction.assign_nearest_keeps_the_first_of_equal_distances_and_uses_manhattan`,
    `construction.assign_nearest_filters_by_mode_and_capacity`,
    `construction.assign_nearest_skips_jobs_without_a_building_and_refuses_an_unknown_builder`).
14. Assigning to a given job refuses with -1 a building without a job, a job whose building object is gone, a finished job,
    a full job and an unregistered builder; on success it sets the manual flag, counts the builder and reports 0
    (construction) or 1 (anything else) (`construction.assign_to_job_sets_the_manual_flag_and_returns_the_kind`,
    `construction.assign_to_job_refuses_unknown_finished_full_and_unmatched`).

### Reallocation
15. Each pass first sets the status of every entry: usable (0) only when its home exists and is open, it is on shift,
    outside the hut, not manually assigned and not in the "directed" state, otherwise 1
    (`construction.reallocate_status_needs_shift_open_home_hut_manual_and_state`). A builder whose home no longer exists
    keeps its old status; the status pass still runs to the end, and then the reallocation stops without assigning or
    clearing anything (`construction.reallocate_a_missing_home_keeps_the_old_status_and_ends_after_the_pass`).
16. With the open-job counter at 0, every assignment is cleared but the manual flag stays
    (`construction.reallocate_without_jobs_clears_every_assignment_but_not_the_manual_flag`).
17. The share per job is the number of usable builders divided by the open-job counter, at least 1. Unfinished jobs flagged
    construction or demolition are filled up to the share and their capacity from the unassigned usable builders in table
    order, and each assigned builder is sent a construct or demolish message
    (`construction.reallocate_spreads_evenly_and_tells_each_builder`,
    `construction.reallocate_respects_capacity_and_hands_the_rest_to_the_extra_pass`). Finished jobs still count in the
    counter but take nobody (`construction.reallocate_skips_finished_jobs_and_gives_the_leftover_to_the_open_one`).
18. Builders left over are spread in an extra pass: the leftover divided by the counter, rounded up, more per unfinished job
    with room; this pass ignores the kind flags and sends the demolish message to a job without the construction flag
    (`construction.reallocate_respects_capacity_and_hands_the_rest_to_the_extra_pass`,
    `construction.reallocate_skips_finished_jobs_and_gives_the_leftover_to_the_open_one`,
    `construction.reallocate_extra_pass_ignores_the_kind_flags`).
19. A job with more builders than the share gives up its first usable, not manually assigned builders, as many as the
    places other jobs lack; the builder's work point is freed and it is then assigned like any free builder
    (`construction.reallocate_takes_surplus_builders_off_a_crowded_job`).
20. The pass refuses (returns false) for a table count outside 0..50, a builder without an object and a job whose building
    is gone; statuses already written in the pass stay
    (`construction.reallocate_refuses_inputs_that_would_crash`).

### Starting work
21. Starting a build needs the building and its definition, otherwise it returns false and does nothing. It appends a job
    flagged "construction" and started, with the definition's times, places the building on the grid, collects work points
    (only with a footprint), counts it, and reallocates builders
    (`construction.start_build_creates_a_started_construction_job_and_reallocates`,
    `construction.start_build_without_a_footprint_collects_no_work_points`,
    `construction.start_build_refuses_a_missing_object_or_definition`).
22. During game start-up a build is not counted and does not reallocate; the building gets a state-change message (state 0)
    instead. A jetty (a building whose key contains "JETTY") is counted but also gets the state-change message instead of a
    reallocation (`construction.start_build_during_the_start_up_counts_nothing_and_does_not_reallocate`,
    `construction.start_build_of_a_jetty_counts_but_sends_the_state_change_instead_of_reallocating`).
23. When the building already has a job, the "started" mark goes to that older job (the first of the building), not to the
    new one; jobs of other buildings stay in front of the new one
    (`construction.start_build_marks_the_first_job_of_the_building_started_not_the_new_one`,
    `construction.start_build_appends_after_the_other_jobs`).
24. Starting a demolition appends a job flagged "demolition" (not started: the building starts it), collects work points
    (with a footprint), tells the humans that the building is being removed, removes the building's repair and cleaning jobs,
    frees its footprint cells, queues the block rebuild of its rectangle and reallocates. Without a footprint no points are
    collected and no cells are freed. It is counted even during start-up. An older job of the same building stays untouched
    (`construction.start_demolish_creates_a_demolition_job_and_tells_the_world`,
    `construction.start_demolish_counts_during_the_start_up_too_and_always_reallocates`,
    `construction.start_demolish_without_a_footprint_neither_collects_points_nor_frees_cells`,
    `construction.start_demolish_keeps_an_older_job_of_the_same_building_and_flags_the_new_one`,
    `construction.start_demolish_refuses_a_missing_object_or_definition`).

### Progress of work
25. Each frame of work on a started job (any non-zero started byte) takes frame time times 0.01 off the remaining work of
    its kind; progress is 1 minus remaining over the total time of that kind (the build time for a construction, the demolish
    time for anything else). The progress uses the remaining work before it is rounded to the stored float
    (`construction.cost_point_takes_frame_time_times_a_hundredth_off_a_construction_job`,
    `construction.cost_point_uses_the_demolition_fields_without_the_construction_flag`,
    `construction.cost_point_progress_comes_from_the_unrounded_remaining_work`). A total of 0 gives an infinite negative
    progress; totals of 2^31 or more are used as unsigned numbers
    (`construction.cost_point_a_total_of_zero_or_above_two_to_the_31_converts_as_unsigned`).
26. Nothing happens for a job whose building object is gone, an object without a job, or a job that is not started
    (`construction.cost_point_does_nothing_without_object_job_or_started_flag`). A job that is already finished keeps being
    worked but does not end again (`construction.cost_point_a_finished_job_is_worked_but_ends_nothing_again`).
27. The work ends when the remaining work is zero or below, or not a number; 0.1 left does not end it
    (`construction.cost_point_the_end_is_reached_at_zero_remaining_work_and_on_nan_not_above`).
28. Ending a construction marks the job finished, sends every builder entry of the job (in table order, repeats too) the
    "construction finished" message, then the building the "builders left the base plate" message with the construct
    message number as data, at once, and lowers the open-job counter
    (`construction.cost_point_ending_a_construction_tells_the_builders_then_the_building`).
29. Ending a demolition sends the building the "builders left the base plate" message with the demolish message number as
    data, after the builders; for land and beach buildings it is delayed by 0, for shallow-water and deep-sea buildings by
    11 seconds. The counter is a plain integer and can go below zero
    (`construction.cost_point_ending_a_demolition_delays_the_building_message_in_water`).
30. The builder count and the job kind are read again after each message, so a builder's reply that changes them is
    respected (`construction.cost_point_reads_the_builder_count_and_the_job_kind_after_the_messages`).
31. Work refuses (returns false) for a started job whose building has no definition (nothing changes), and for a builder
    count outside 0..50 when the job ends; the job is then already marked finished and its remaining work taken off, but no
    message is sent and the counter is not lowered (`construction.cost_point_refuses_where_it_would_crash`).

### Save and game runs
32. Saving and loading keeps the jobs and the fence records of a job, each record as saved
    (`tests/construction_load.txt`, `tests/construction_fence_load.txt`).
33. In a running game spawned buildings finish construction and a demolished building disappears after the demolition time
    (`tests/construction_finish.txt`).

## Edge cases
- Removing a builder whose id is in the table twice: the first entry's job is given back and both entries lose their
  assignment, so the second entry's job keeps its builder count and token; only the first entry leaves the table
  (`construction.remove_builder_clears_every_entry_with_the_id_after_the_first_releases`). Freeing such a builder from its
  job behaves the same (`construction.free_builder_of_job_second_entry_with_the_id_finds_no_job`).
- Removing an id that is not registered works on the entry just behind the table: the job of the builder whose id is stored
  there is given back, and the table still shrinks by one
  (`construction.remove_builder_of_an_unregistered_id_works_on_the_stale_entry`).
- The tile distance and the rebuild rectangle are 32-bit differences that wrap
  (`construction.assign_nearest_distance_wraps_like_32_bit_arithmetic`,
  `construction.start_demolish_rebuild_rectangle_is_a_32_bit_difference`).

## Not covered by this spec yet
Tests exist; the rules still have to be written: footprint placement and clearing (`construction_footprint_test.cpp`),
work-point claiming and entry points (`construction_workpoints_test.cpp`), the finish step with grid, refunds and hoarding
(`construction_finish_test.cpp`), the building-side construction state (`building_construction_test.cpp`).

## Not covered by tests
- Reallocation is also asked for by the finish step, a builder's script call and the staff update; only the calls from
  starting work and from adding and removing builders are pinned here.
- `tests/construction_queries.txt` only checks that the query, assignment, release and work-point entry points are called
  in a running game; it pins no rule.

## Tests
`tests/unit/construction_queries_test.cpp`, `construction_assign_test.cpp`, `construction_reallocate_test.cpp`,
`construction_start_test.cpp`, `construction_cost_point_test.cpp`; game scripts `tests/construction_load.txt`,
`construction_fence_load.txt`, `construction_finish.txt`.
