# Golden images

Scripted tests compare presented frames with reference PNGs so rendering changes show up automatically. References are
recorded from the current build; large ones live in `run/golden/` (git-ignored), small derived ones may be committed
(`tests/golden-images/`).

## Metric (`src/core/image_compare.h`)
Pure module in `obl_common`, shared by the unit tests, the tool and the in-game assertion. Inputs: RGB555 (the game's
16-bit buffer), RGB565 or XRGB8888 with any pitch; 5/6-bit channels expand to 8 bits by bit replication, exactly like the
PNG writer of `shot`, so a buffer and its PNG compare as identical. `Compare(actual, expected, tolerance)` reports size
match, maximum channel error, mean absolute error and the count and percentage of pixels whose channel error exceeds the
tolerance; an optional diff image shows differing pixels in red over a darkened reference.

## Tool
```powershell
./build/x64/bin/Debug/obl_imgdiff.exe actual.png expected.png [--tol N] [--max-percent P] [--diff out.png]
```
Prints one `key=value` line; exit 0 = equal within the limits, 1 = different or another size, 2 = usage or I/O error.

## Assertion `expect-shot <name> [tol] [max-percent]`
Waits for the next presented frame, saves it as `run/captures/<name>.png` and compares it with `run/golden/<name>.png`.
Passes when the share of pixels above `tol` (default 0) does not exceed `max-percent` (default 0); on failure the diff
image is `run/captures/<name>_diff.png`. A missing reference is a `SKIP` with recording instructions. Record references
only deliberately (changed frame in the script, intended visual change):
```powershell
./tools/autotest.ps1 tests/golden_frames.txt -UpdateGolden
./tools/runtests.ps1 -Filter golden* -UpdateGolden
```

## GUI image sets
`tools/gui_image_golden.ps1` checks one committed set per pack: `tests/golden-images/m6-gui/` with `m6_gui_golden.txt` on
the imported data (`-Pack private`, the default) and `tests/golden-images/m6-gui-l0/` with `m6_gui_golden_l0.txt` on the
own L0 pack (`-Pack l0`, sets `OBL_PACK=l0`). `-Record` runs the script twice and stores the images only if both runs are
identical. The L0 set covers only the dialogs L0 defines. The post-run check of both scripts is the portable
`tools/gui_image_check.py` (Pillow, same on Windows and Linux); `-CheckOnly` calls it. Windows and Linux frames are identical.

## Deterministic frames
A frame can be a reference only if two runs give identical pixels:
- `seed 1` + `fixed-dt 0.02` from the first frame (sea waves and smoke follow the game clock);
- only frame-counted waits (`await frames`, `step`), `move`/`down`/`up` instead of `click`/`wait`;
- `# @gameini Video.ResWidth/ResHeight` (the game remembers the resolution), `NudgeScroll=0` (the hidden window's cursor
  must not scroll the camera) and fixed `SeaEffects`, `LightEffects`, `Keyline`;
- report probes must not read uninitialized game fields.
Check with `./tools/determinism_check.ps1 <script>` (two or three `IDENTICAL` runs) before recording.
