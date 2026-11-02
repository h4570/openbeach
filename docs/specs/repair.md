# Spec: repair manager

## Purpose
A queue of maintenance requests for buildings and scenery (bins, plants) that mechanics take one at a time: breakdown
repairs, inspections, scenery work and watering.

## Vocabulary
- **Job type**: 0 = inspection, 1 = scenery work, 2 = breakdown, 3 = watering. **Urgent**: a flag on a job.
- **Lists**: queued building jobs, queued scenery jobs and the active jobs (the ones a mechanic holds).
- **Distance**: the squared distance from the asking mechanic to the object (the test with a building at 10 units gets 100).
- **Kind** asked by a mechanic: 0 = inspections, 1 = scenery work, 2 = breakdowns, 3 = watering.

## Rules
1. A request with priority 0 (inspection) or 2 (breakdown) lands in the building list, one with priority 1 (scenery) in the
   scenery list. A request for an unknown object is ignored; a new job records the game time it was called (`repair.add_job_queues_by_priority_and_counts_repeats`).
2. A repeated request for a queued object counts in `calls` and only raises its type (inspection to breakdown, never back);
   no list grows (`repair.add_job_queues_by_priority_and_counts_repeats`). The summary counters (`breakdownCount`) are not
   changed by the queue calls (same test).
3. A request for an object whose job is already active raises that job's type only and queues nothing
   (`repair.add_job_on_active_job_raises_type_only`).
4. An urgent request chooses the list by the object's class (building: type 2, anything else: type 1), marks the job
   urgent, and promotes a queued normal request of the same object to urgent with that type, which counts as a repeat; an
   unknown object is ignored. On an active job it marks it urgent
   and raises its type (`repair.add_urgent_job_chooses_list_by_object_class`,
   `repair.add_urgent_job_on_active_job_marks_it_urgent`).
5. Claiming a job puts it in the active list as "not yet repairing" and marks the object claimed; claiming an object that is
   already active is ignored. A "started" call sets `repairing`; calls for unknown objects change nothing. Releasing
   removes the active job; the active counter never goes down (`repair.claim_release_started`).
6. A scan for a kind considers the urgent jobs and the normal jobs of that kind that have waited more than the waiting
   time (exactly 5 is not enough, 5.001 is); a normal job's object must not be claimed by a cleaner. It records the squared
   distance on each candidate and keeps the nearest urgent job and the nearest job of the kind, the first on ties (`repair.scan_needs_waiting_time_and_free_object`, `repair.scan_picks_nearest_per_kind_and_ignores_other_kinds`,
   `repair.scan_ties_keep_the_first_candidate`).
7. For urgent jobs a building is skipped when a mechanic already claimed it, a scenery job when a cleaner did. The scenery
   list is only scanned when the building pass found no urgent job (`repair.scan_urgent_skips_scenery_pass_and_uses_mechanic_flag_for_buildings`).
8. Finding a job claims and dequeues the winner and reports its type, leaving the type output untouched when nothing
   fits (`repair.find_job_claims_and_dequeues_the_winner`). An urgent job wins whatever the asked kind and distance and is
   held as urgent; with none, the nearest job of the asked kind is taken as a normal job
   (`repair.find_job_prefers_urgent_and_falls_back_to_inspection`).
9. Watering: a plant search draws a random plant (`random % plant count`, repeats allowed) as many times as there are
   plants and takes the first that qualifies; dying, non-waterable and already held plants never qualify, and no plants
   means no draw. The general call (kind 3) wants a plant closer than 200 units and holds it as an urgent watering job
   (`repair.find_job_watering_uses_plants_within_radius`). The watering call wants a plant farther away: a second draw per
   waterable plant decides the distance, more than 150 units when it divides by 3, more than 120 otherwise; dying and
   non-waterable plants cost no second draw (`repair.find_plant_far_rule_draws_a_second_time`). It holds the plant as a
   normal watering job (`repair.find_watering_job_claims_a_normal_watering_job`).
10. Returning a job removes it from the active list, frees the object, and requeues it in the list of its kind with its
    urgent flag; watering jobs are not queued again. A return without an active record is a plain request
    (`repair.return_job_requeues_by_kind`).
11. A mechanic holding a normal job switches to another normal job only when that one is nearer; farther or equal ones lose
    (`repair.look_for_better_job_normal_needs_nearer_or_higher_type`). An urgent job replaces a normal one, and the old job
    goes back to the queue (`repair.look_for_better_job_urgent_replaces_normal`). A holder of an urgent job yields only to
    a nearer urgent job (`repair.look_for_better_job_urgent_holder_only_yields_to_nearer_urgent`). Urgent watering jobs
    are never replaced (`repair.look_for_better_job_ignores_urgent_watering`), a normal job never replaces an urgent one,
    and an object nobody holds finds nothing. The search is for the kind of the held job, so a far breakdown does not tempt
    an inspector (`repair.look_for_better_job_inspection_gives_way_to_breakdown_only_when_kinds_allow`).
12. Claiming a specific object (the player directs a mechanic): a queued request is taken from its list and its own type
    is reported, but a building is held as an urgent breakdown whatever the request said; a building nobody asked for is
    held as a breakdown; other scenery is scenery work (type 1), a waterable one watering (type 3); every claim is urgent
    and marks the object as directed and held. Guests, unknown objects and objects a cleaner or a mechanic holds are
    refused with -1 and leave the type output untouched (`repair.claim_object_building_takes_the_queued_request`,
    `repair.claim_object_scenery_and_refusals`).
13. Job status for an object: 2 = none (building without a request), 3 = a queued breakdown, 4 = a queued request of another
    type, 5 = claimed, 6 = being repaired, 0 when there are no mechanics. The job type is the type a mechanic holds the
    object under, -1 when nobody holds it. Scenery is looked up in the scenery list only (`repair.get_job_status_and_type`).
14. A fresh manager has three empty lists and zero counters; of the saved fields only the distance is scratch; removing a
    job takes one pending request, the building list first, and leaves active jobs and the counters alone; the urgent count
    is the building jobs of type breakdown (urgent flag or not) plus all scenery jobs (`repair_lifecycle.a_new_manager_has_three_empty_lists_and_zero_counters`,
    `repair_lifecycle.field_table_marks_only_the_distance_as_scratch`,
    `repair_lifecycle.remove_job_takes_the_first_pending_request_building_list_first`,
    `repair_lifecycle.count_urgent_jobs_is_breakdowns_plus_all_scenery_jobs`).

## Game scripts
- `tests/command_call_repair.txt`: the player's call-repair command marks the building as called; an unknown id is
  rejected.
- `tests/building_repair.txt`: a building broken by lightning is repaired in a running game; only the building's message
  counters are checked.

## Not covered by tests
- That a normal job of a higher type replaces a held job of a lower type although it is farther (the code allows it; the
  scan only offers jobs of the held kind, so no test reaches it).
- What the numbers 3 to 6 of job status mean to the callers (the scripts); the spec only repeats what the test asserts.
- The scenery list's own selection rule beyond the single urgent-scenery scan case.

## Tests
`tests/unit/repair_manager_test.cpp`, `tests/unit/repair_lifecycle_test.cpp`, game scripts `tests/building_repair.txt`,
`tests/command_call_repair.txt`.
