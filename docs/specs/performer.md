# Spec: show performers

## Purpose
The six show performers (aerobics, cabaret, catwalk, singer, weightlifter, wet T-shirt) join their building's show, pick a
look, and act it out through states and messages. The tests are in `tests/unit/performer_test.cpp`; test names below are
quoted as they appear there.

## Rules
1. Aerobics, cabaret, catwalk, weightlifter and wet T-shirt know the states 0, 1, 4 and 6; the singer knows 0, 1 and 6
   ("Performer HasState tables").
2. Looks are chosen by random draws and clamped to the number of palette entries ("Performer variants draw ... clamp to the
   palette count"):
   - Aerobics: first draw picks the tens (modulo 6), second the units (modulo 7, plus 2); look 0 is "Gail", look 5 "Shaz".
   - Cabaret: looks 0..3 are "GAIL", "SHAZ", "FAT_BLOKE", "FIT_BLOKE"; the first two use two draws, the others one; any
     other look only stores the number and creates nothing.
   - Catwalk and singer use the sprite their constructor made; the tens are modulo 4 for the catwalk and 6 for the singer.
   - Weightlifter: three draws (palette, parity, units); an even parity gives the units modulo 20 plus 41, an odd parity
     modulo 30 plus 11; a small palette clamps to its last entry.
   - Wet T-shirt: look 3 is the contestant ("wtsc", one draw); every other look burns one draw first and uses "wts".
   - A computed palette index at or above the palette size becomes the last entry.
3. A performer saves and loads its fields in a fixed order: action state, type, owner, finished flag, then (except for the
   cabaret) the action point, with the singer and weightlifter adding an attached sprite and the wet T-shirt a contestant
   field ("Performer Save and Load visit the records ..."; keys also in `performer.field_keys_match_the_save_bridge`).
4. A performer joining a building takes the building as owner and registers with it. The aerobics and wet T-shirt performers
   also move to a fixed stage offset and reset the action point's height to 0; the cabaret takes no offset
   ("Performers join the show and take the stage point").
5. On joining, a catwalk is hidden for building rotations 1 and 2 only; a singer's microphone turns to (11 - 2 x rotation)
   modulo 8, clamped to the last direction when the animation has fewer ("A catwalk hides for rotations 1 and 2 only ...").
6. Aerobics and the singer walk on stage in state 1 and off it in state 6; the cabaret appears at the stage exit and walks to
   the mark of the building's rotation ("Aerobic and Singer walk on stage ...", "Cabaret appears at the stage exit ...").
7. Per-act state and message sequences are pinned, including the order of sends: catwalk marks, next actor and way out
   ("Catwalk states ...", "Catwalk ReceiveMessage ..."); weightlifter walk, weights and release ("WeightLifter states ...",
   "WeightLifter ReceiveMessage ..."); wet T-shirt contestants, compere, carrier, wet and dry dances and hand-off
   ("WetTShirt states ...", "WetTShirt ReceiveMessage MT_MOVEIMHERE ...", "WetTShirt dry dance transitions ...", "WetTShirt show
   requests the carrier ...", "WetTShirt show hands off ..."); aerobics ("Aerobic ReceiveMessage ..."); singer body and
   microphone kept in step ("Singer ReceiveMessage ..."); cabaret dance, teleport, exit and costume ("Cabaret ReceiveMessage ...").
8. Playing an animation sets its type and hands the end state to the message slot 9 ("PlayAnimation sets the animation type ...").
9. Constructors, destructors, update and draw are covered by "Performer constructors, destructors, Update and Draw".

## Not covered by tests
- The look of a performer on screen (sprite art); the full show from the audience's view is only checked by the game script
  `tests/performers.txt`, which spawns every performer in all four rotations.
- Rule 7 is listed by test, not restated in detail: read the test for exact message order.

## Tests
`tests/unit/performer_test.cpp`; game script `tests/performers.txt`.
