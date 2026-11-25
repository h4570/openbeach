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
which checks two runs against each other, then copy `run1/l0_mods_combined.replay` from the printed artifacts directory
(the tool's last replay step runs without `OBL_MODS`). A new mod feature adds its test mod to the set, keeps it free of
names the others use, and re-records the replay with the reason in the commit body.

`l0_script` stays out of the set: it submits speed and pause commands, which would stop the day.

## Conflict set
`check_mods_engine.py` checks the conflict rules on the pairs `l0_conflict_a`/`l0_conflict_b` (`conflicts`, refused),
`l0_takeover_a`/`l0_takeover_b` (the same input area: the later mod wins, in either order) and `l0_busker`/`l0_crlf` (the same
object-script state, refused naming both mods). `tests/l0_mod_conflict_refused.txt` checks the last pair in the game: the start
stops with a message naming both mods.
