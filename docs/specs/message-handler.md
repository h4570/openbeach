# Spec: message handler

## Purpose
Delivers the messages that objects send each other. A message is either delivered at once or queued with a time; the
queue is worked through once per frame. While the game is paused, messages are held back so nothing acts during the pause.

## Vocabulary
- **Message**: sender, receiver, type, data and a time. A sender of the special "silent" value is delivered even while paused.
- **Delayed list**: the queued messages, in the order they were queued.
- **Now**: the game clock. All times are single-precision floats and are added as such.

## Rules
1. Posting delivers at once unless the game is paused. While paused, every message is queued (receiver and type do not
   matter), except those from the silent sender, which are still delivered at once
   (`message_handler.post_defers_only_when_paused`, `message_handler.post_delivers_at_once_or_queues_when_paused`).
2. Posting stamps the caller's message with the current time. A message queued because of the pause gets the current time
   plus 0.0001, so it is due on the next pass but not in the same instant (`message_handler.times_are_float_sums`,
   `message_handler.post_delivers_at_once_or_queues_when_paused`).
3. Posting with a delay queues the message at now plus the delay and writes that time into the caller's message; nothing is
   delivered. The sum is a float sum, so a small delay added to a large time can vanish
   (`message_handler.post_delayed_queues_at_now_plus_delay`, `message_handler.times_are_float_sums`).
4. A queued message is due when its time is not later than now: equal is due, a later time is not, and a time that is not a
   number counts as due (`message_handler.due_boundary_and_nan`).
5. The per-frame update walks the delayed list in order and delivers every due message in that order; messages that are not
   due stay queued in their order (`message_handler.update_delivers_due_in_order_and_defers_new_posts`).
6. A message posted during a delivery, even one that is already due, is not delivered in the same update: it waits at the
   end of the delayed list for the next update (`message_handler.update_delivers_due_in_order_and_defers_new_posts`).
7. Clearing a receiver's delayed messages removes all of its queued messages except those of five types that survive
   (opinion update 0x3f, look around 0x54, finish action 0x38, move to queue point 0x1f, move here 0x16), and returns how
   many were removed. Messages of other receivers stay, and the kept ones stay in their order
   (`message_handler.clear_keeps_surviving_types`).
8. The two message builders fill only the fields they are given: the five-field form sets sender, receiver, type, flags and
   data and leaves the rest as it was; the seven-field form also sets the float data and the extra value and leaves the time
   as it was (`message_handler.init_fields_leave_the_suffix_bytes_untouched`).

## Edge cases
- Any sender other than the silent value is held while paused, also sender 0 and negative senders
  (`message_handler.post_defers_only_when_paused`).
- The per-frame update moves the due messages to a working list and delivers them from there; the working list is empty
  when the update returns (`message_handler.update_delivers_due_in_order_and_defers_new_posts`).

## Not covered by tests
- That a silent-sender message posted while paused gets the plain current time, without the pause delay (the code does;
  the test checks only that it is delivered).
- Dispatch of a delivered message to the receiving object by type; only the counters of calls in a 900-step crowd run are
  checked (`tests/message_handler.txt`: update, dispatch and delayed-post counters rise, the working list is empty at
  the frame boundary, the world hash has fields).
- What the individual message types mean to the objects; those belong to the object families' specs.

## Tests
`tests/unit/message_handler_test.cpp`, game script `tests/message_handler.txt`.
