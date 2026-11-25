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
