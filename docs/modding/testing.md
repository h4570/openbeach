# Modding tests

The checker failure set in `tests/mods/broken/` runs in one process on the L0 pack. The test checks each diagnostic
and a failing exit status. The same test script checks each working test mod and the official frame counter separately,
activating declared requirements before the mod. It also covers a source directory outside `mods/` and an empty list.

```sh
python tools/tests/check_mods_engine.py build/linux-sim-x64/bin/obl-sim
```

Pass the built `OpenBeach.exe` on Windows. Add failing fixtures under `broken/` when a new format or startup check
introduces a diagnostic. Keep working fixtures beside that directory and give each feature its own game test for behavior
after startup.

## Combined set
`tests/l0_mods_combined.txt` activates the test mods that name nothing in common at once: world mods (text patch, catalog
patch, building, guest with its script, two world scripts) and presentation mods (overlay, settings, actions, state, GUI
patch). It plays across the first midnight and checks that the world scripts ran. `tests/replays/l0_mods_combined.replay`
records the same day; `tests/replay_l0_mods_combined.txt` plays it with the whole set, so the world hashes of the set
are a reference on every host that runs replays. The replay's `mods` line names the world mods only, because presentation
mods never feed the world. Re-record it with `tools/replay.ps1 -Record tests/replays/l0_mods_combined.record.txt -Pack l0`,
which checks two runs against each other and plays the new file once more; `-Record` takes `OBL_MODS` from the recording script's
header, so that last step runs with the same mods (`-Mods a,b` sets them by hand for `-Replay`). Then copy `run1/l0_mods_combined.replay`
from the printed artifacts directory. A new mod feature adds its test mod to the set, keeps it free of
names the others use, and re-records the replay with the reason in the commit body.

`l0_script` stays out of the set: it submits speed and pause commands, which would stop the day.

## Conflict set
`check_mods_engine.py` checks the conflict rules on the pairs `l0_conflict_a`/`l0_conflict_b` (`conflicts`, refused),
`l0_takeover_a`/`l0_takeover_b` (the same input area: the later mod wins, in either order) and `l0_busker`/`l0_crlf` (the same
object-script state, refused naming both mods). `tests/l0_mod_conflict_refused.txt` checks the last pair in the game: the start
stops with a message naming both mods.

## Limits
The generic object functions of a mod (`CreateObject` and the rest of `docs/modding/reference/object-scripts.md`) stop the game with a
message that names the function and the script line when a script passes a limit. The test mod `l0_limits` makes every visitor
create a helper that stays in the resort, and two game tests expect the fatal error with the headers `# @expect-exit 1` and
`# @expect-log <regex>` (`docs/testing/game-tests.md`):
- `tests/l0_mod_limit_day.txt`: eight visitors reach the 20 objects a day.
- `tests/l0_mod_limit_type.txt`: one visitor makes about 16 helpers a day, under the daily limit, and the 41st of the type stops the game
  on a later day. The test lifts the campaign's day limit first (`world-script-change STATE_WORLD_AFTERGAME`).

A new limit adds a test of the same shape: a mod that passes it, the exit code and the message.

## Mods screen
`tests/l0_mod_screen.txt` switches a presentation mod on and changes its settings, `l0_mod_screen_switch.txt` switches it on, off and on
again (the script starts, stops and starts again), `l0_mod_screen_conflicts.txt` and `l0_mod_screen_takeover.txt` check the messages for
conflicting pairs, and `l0_mod_install.txt` installs and removes a ZIP through the screen. The screen is only reachable from the main
menu, so a world mod switched on there applies on the next start (`l0_mod_screen.txt` checks the note).

## Frame budget
`tests/l0_mods_frame_budget.txt` steps 500 fixed frames with the combined set's presentation mods active and checks the step's
`on-frame`, `update` and `render` milliseconds (`expect-last-range`). The limits are about 15 times a Debug build's cost, so they only
catch a mod that stops being cut by its instruction budget or work that grows each frame, not a noisy runner.

## Start-up with many mods
`check_mods_engine.py` loads 120 small world mods (each with its own text patch) in one run and requires the checker to finish within
60 seconds (it needs a few).

## Fuzzing
The same file mutates the working fixtures (a flipped, dropped, repeated or truncated piece, a changed value, deep nesting, control
characters) in manifests, patches and scripts and runs the checker on each mutant: refusing it is fine, a crash, a sanitizer report
or a hang fails. The default is 16 seeded mutants on every push. A long run is manual and never part of the default lanes:

```sh
python tools/tests/check_mods_engine.py <engine> --fuzz-iterations 500 --fuzz-seed 7   # or OBL_FUZZ_ITERATIONS / OBL_FUZZ_SEED
```

Seeds are reproducible, so a failure names its seed and iteration. Fuzzing of the catalog and ZIP index parsers stays with their unit
tests (`mod_catalog_test.cpp`, `mod_archive_test.cpp`).

## Bots and chaos
Bot and chaos run files take `# @env NAME=VALUE` lines like game test scripts. `tests/bots/l0/build_tree_mods.bot` plays the L0 bot with the
combined set (its own bands, `tests/bots/l0/build_tree_mods.bands.json`: the mods change the economy) and `tests/chaos/l0_mods.chaos`
plays the chaos bot with it, including the fresh-process replay. Both lists must equal the combined set's; the chaos run has no `save-at`
because the L0 scenario keeps a message balloon up for the whole run. `tools/ci/l0_sim_tests.sh` stages the test mods for the bot run.
Longer chaos runs with mods are the manual `python tools/chaos_runs.py --filter l0_mods --seeds 1 2 3 --ticks 20000`.

## Checks left to a person or a device
- Dropping a ZIP onto the browser build and installing from its mods screen with a real file chooser or drag and drop.
- The mods screen and an install on an Android device (touch, back button, the file picker).
- Frame times on a phone with the presentation mods of the combined set.
