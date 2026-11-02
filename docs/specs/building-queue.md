# Spec: building queue

## Purpose
Order guests waiting for service, place them on the ground, and notify them when service or closure changes the queue.

## Vocabulary
The serve point is the start of the line; the tail is its current end. Each queued guest has a corresponding commodity.
Short queues (limits 1 through 10) have precomputed standing points; other limits use the stored tail.

## Rules
1. Construction starts with no guests or commodities. A limit of 4 builds standing points; limits 0, 11 and 40 do not
   and start the tail at the serve point (`building_queue_lifecycle.construct_with_points_table`,
   `building_queue_lifecycle.construct_limits_without_table`).
2. Enqueue refuses a full guest or commodity list with a queue-full notification. A zero queue limit instead reports
   building-full (`building_queue_ops.enqueue_refuses_a_full_queue_with_queue_full`,
   `building_queue_ops.enqueue_refuses_when_only_the_commodity_deque_is_full`,
   `building_queue_ops.enqueue_into_a_building_without_a_queue_says_building_full`).
3. Enqueue without movement appends the guest and commodity without messages or changing the tail
   (`building_queue_ops.enqueue_without_moving_only_stores_the_guest`).
4. Moving enqueue uses the standing point for the new length, or finds a new point for a long queue. It sends the join
   notification before the movement message. Unsuitable standing ground triggers a rebuild; movement coordinates
   truncate toward zero (`building_queue_ops.enqueue_takes_the_point_table_entry_for_the_new_length`,
   `building_queue_ops.enqueue_asks_find_next_point_for_long_queues`,
   `building_queue_ops.enqueue_rebuilds_when_the_tail_cell_is_not_a_stand_point`).
5. A join listener can change the tail; subsequent movement uses that changed tail
   (`building_queue_ops.enqueue_reads_the_tail_again_after_the_join_message`).
6. Dequeue returns the first guest and commodity and moves remaining guests forward with a three-second delay.
   An empty guest or commodity list fails without changing the output guest. Removing the only guest sends no movement
   (`building_queue_ops.dequeue_returns_the_head_and_moves_the_rest_up`,
   `building_queue_ops.dequeue_of_an_empty_queue_is_false`,
   `building_queue_ops.dequeue_with_a_single_guest_sends_nothing`).
7. Forward movement starts from the serve point plus the running direction for queues without precomputed points;
   short queues use the next standing point before rebuilding
   (`building_queue_ops.dequeue_without_a_table_computes_the_spot_from_serve_point_and_direction`,
   `building_queue_ops.dequeue_with_a_table_reads_entry_i_plus_one`).
8. A long queue's tail query returns its stored tail. A short queue uses its final standing point and rebuilds unsuitable
   ground; the returned point is read again after rebuilding
   (`building_queue_ops.tail_point_of_a_queue_without_a_table_is_the_stored_tail`,
   `building_queue_ops.tail_point_is_the_last_table_entry_when_its_cell_is_fine`,
   `building_queue_ops.tail_point_rebuilds_for_unfit_cells`,
   `building_queue_ops.tail_point_reads_the_table_again_after_the_rebuild`).
9. Finding the next point prefers straight ahead, then a usable side, then backward. When both sides qualify, an odd
   random draw chooses the first and an even draw the second. With no usable route it keeps the blocked forward point
   (`building_queue_points.find_next_goes_straight_on_when_the_cell_will_do`,
   `building_queue_points.find_next_takes_the_only_open_side`,
   `building_queue_points.find_next_tosses_a_coin_when_both_sides_will_do`,
   `building_queue_points.find_next_turns_round_and_keeps_the_blocked_cell_as_a_last_resort`).
10. On the tested axis, the first side gets a weaker retry before the second side wins
    (`building_queue_points.find_next_axis_retries_the_first_side_with_a_weaker_test`).
11. Rebuilding continues straight over open ground and preserves earlier points when begun in the middle. When turning,
    it prefers the longer free side run, with ties going to the first; with neither side open it keeps the forward step.
    A long queue's rebuild simply returns its tail
    (`building_queue_points.rebuild_walks_the_table_straight_on_over_open_ground`,
    `building_queue_points.rebuild_from_the_middle_continues_in_the_running_direction`,
    `building_queue_points.rebuild_sideways_takes_the_longer_free_run_and_ties_go_to_the_first`,
    `building_queue_points.rebuild_off_axis_sideways_cells_are_a_and_b`,
    `building_queue_points.rebuild_of_a_long_queue_is_the_tail`).
12. Clearing notifies guests in queue order of closure or breakdown, empties both lists and resets the tail to the serve
    point. A listener shrinking the line can shorten notification traversal
    (`building_queue_clear.every_queued_guest_hears_the_kind_in_order`,
    `building_queue_clear.both_deques_are_emptied`,
    `building_queue_clear.empty_queue_sends_nothing_and_resets_the_tail`,
    `building_queue_clear.a_listener_that_shrinks_the_queue_stops_the_loop_early`).
13. Boarding first sends service to the boarding guest with the queue length, then sends boarding to waiting guests
    at delays 0, 0.5, 1.0 seconds and so on. It empties both lists and resets the tail; an empty queue still sends service
    (`building_queue_clear.board_serves_the_guest_then_tells_every_queued_guest_half_a_second_apart`,
    `building_queue_clear.board_empties_both_queues`, `building_queue_clear.board_with_an_empty_queue_only_serves`).
14. Destruction clears both lists. Loading replaces guest and commodity lists independently, leaving the existing tail
    and standing points alone (`building_queue_lifecycle.destruct_clears_and_frees_the_table`,
    `building_queue_lifecycle.load_replaces_deques`).

## Edge cases
- A negative loaded count reads no entries (`building_queue_lifecycle.load_signed_count`).
- Enqueue and tail queries do not use identical ground rejection conditions; the explicit flag combinations are pinned
  by `building_queue_ops.enqueue_rebuilds_when_the_tail_cell_is_not_a_stand_point` and
  `building_queue_ops.tail_point_rebuilds_for_unfit_cells`.

## Not covered by tests
The queue tests assert movement requests and delays, not whether guests visibly form a convincing line.

## Tests
`tests/unit/building_queue_lifecycle_test.cpp`, `tests/unit/building_queue_ops_test.cpp`,
`tests/unit/building_queue_points_test.cpp`, `tests/unit/building_queue_clear_test.cpp`.
`tests/building_queue.txt` checks live queue operations and queue recreation across a world reload through call counts.
