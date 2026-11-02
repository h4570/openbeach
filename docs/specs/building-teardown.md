# Spec: building teardown

## Purpose
Release a building's owned resources when it is destroyed.

## Rules
1. Release the animation sprites counted by the definition, then the shortage and repair icons, stop the building's
   sound, and finish base destruction. Sprites beyond the definition's count are not released. The sound-playing flag
   is cleared (`building_teardown.bare_building`).
2. With optional resources present, delete the queue, delete the event schedule, release the ball, then release the
   icons and stop sound. Release the construction sprite before base destruction and clear that sprite reference
   (`building_teardown.members_in_order`).
3. Destruction empties the entire thought list, including thoughts without text
   (`building_teardown.thoughts_are_deleted`).
4. Destruction with deletion disabled still performs base destruction. With deletion enabled, object deletion follows
   base destruction (`building_teardown.deleting_destruct_flags`).
5. Removing buildings through the world invokes building deletion at least twice in the two-building scenario
   (`building_destruct.txt`).

## Edge cases
A building without a queue or event schedule still releases its icons and stops sound (rule 1 test).

## Not covered by tests
The deletion decision uses the lowest flag bit; the unit test supplies only flag values 0 and 1.

## Tests
`tests/unit/building_teardown_test.cpp`, `tests/building_destruct.txt`.
