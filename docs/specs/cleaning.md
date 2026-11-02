# Spec: cleaning manager

## Purpose
A queue of cleaning requests (litter, bins, toilets) that cleaners take one at a time, with an urgent search and a scored
search by distance.

## Vocabulary
- **Request** (queued job): object id, urgent flag, repeat count `calls`, time it was called. **Active job**: a request a
  cleaner holds, with a "cleaning" flag that is set once the cleaner starts.
- **Kinds of work**: litter, bin, toilet. Scenery named like a bin is a bin; other scenery counts as litter work; a toilet
  is a building whose behavior is "toilet" (not one that is only named like it).

## Rules
1. A request puts the object at the back of the queue with the game time and zero repeats; a repeated request for a queued
   object counts in `calls`. The active list is not consulted: a claimed object queues again
   (`cleaning.add_job_queues_and_counts_repeats`).
2. An urgent request for a queued normal request replaces it at the back of the queue as urgent, with the count and time
   started over; later requests (urgent or normal) for it are counted and it stays urgent
   (`cleaning.add_urgent_job_replaces_normal_request_at_the_back`).
3. An urgent request for an active job marks that job urgent and queues nothing; for an object with no request it queues a
   new urgent request (`cleaning.add_urgent_job_on_active_job_or_new_object`).
4. Claiming puts a job in the active list (not yet cleaning) and counts it; claiming an already active object is ignored;
   releasing removes it and lowers the counter, an unknown release changes nothing. Unlike the repair queue the counter
   goes down (`cleaning.claim_and_release_keep_the_active_counter`).
5. Job status for an object: 2 = no request, 3 = queued, 4 = claimed, 5 = being cleaned, 1 = when no cleaners are employed.
   Removing a request deletes it from the queue and an unknown id does nothing (`cleaning.remove_job_and_status`).
6. "Started cleaning" marks the active job only when the object exists and the job is active
   (`cleaning.started_cleaning_needs_the_object_and_an_active_job`).
7. "Did not clean" frees the object, takes the job from the active list (counter down) and queues it again with the same
   urgency; without an active job it is just a repeated request (`cleaning.did_not_clean_requeues_with_the_same_urgency`).
8. Claiming a specific object: litter returns work kind 0, a bin 1, a toilet building 2; other scenery is claimed too but
   returns -1; guests, unknown objects and objects already held by a cleaner or a mechanic are refused and not claimed.
   A queued request of the object is removed, and the job is active and urgent
   (`cleaning.claim_object_claims_building_or_scenery_and_names_the_work`). The toilet kind comes from the building's
   behavior, not its name (`cleaning.toilet_kind_comes_from_the_behavior_not_the_name`).
9. The urgent search considers only urgent requests, in the kinds the caller allows (litter and other scenery need the
   scenery flag, bins their flag, toilets the buildings flag); the last matching urgent request wins. The winner is
   claimed and its kind is reported; on failure the kind output is untouched
   (`cleaning.find_urgent_job_last_urgent_match_wins`).
10. The urgent search skips a vandalised bin, a bin held by a mechanic, a toilet under construction or demolition, an object
    held by a cleaner, and requests with a time stamp of 0 (`cleaning.find_urgent_job_filters`).
11. The scored search ranks every queued request by tile distance to the cleaner minus a kind bonus (litter 50, bin 70,
    toilet 100) and takes the lowest score; ties keep the first request. The winner is claimed as a normal (not urgent)
    job and removed from the queue; an empty queue answers -1 before the position is converted
    (`cleaning.find_best_scored_job_ranks_by_tile_distance_and_kind_bonus`,
    `cleaning.find_best_scored_job_drops_requests_of_vanished_objects`).
12. The scored search respects the kinds the caller allows and writes the kind only on success. A bin is accepted unless it
    is vandalised or held by both a cleaner and a mechanic; a toilet needs no construction or demolition and no claims;
    litter held by a cleaner is skipped (`cleaning.find_best_scored_job_capabilities_and_claims`).
13. Requests whose object no longer exists are deleted during the scored search
    (`cleaning.find_best_scored_job_drops_requests_of_vanished_objects`).
14. A new manager has two empty lists and both counters at 0 (`cleaning_lifecycle.a_new_manager_has_two_empty_lists`).

## Not covered by tests
- The distance measure is "tile distance" as the test's numbers show (10 for 10 tiles on one axis, 60 for 30 and 30), a
  sum of the axis differences; no test names it.
- Saving and loading of the manager (the lifecycle test only covers the fresh state).

## Tests
`tests/unit/cleaning_manager_test.cpp`, `tests/unit/cleaning_lifecycle_test.cpp`.
