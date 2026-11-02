# Determinism

Goal: the same script produces the same report values and world hashes on every run, and long stretches of game time take
seconds. All commands below exist only in script mode.

## How the game measures time
Each frame `Game_Update` advances the game clock by the real frame time, and the world advances by
`frame delta x speed multiplier` (clamped to 4 s). Everything in the simulation (desires, movement, event timers) uses that
delta or the world hour. Randomness comes from the game's CRT `rand()`, seeded once at start-up from the time.

## Commands
| Command | Effect |
|---|---|
| `seed <n>` | seeds the game's `rand()` (resets the `rand-calls` counter of the thread) |
| `fixed-dt <s>` / `off` | every following frame gets this delta (also during `await`, `boot-gameplay`, `load-slot`) |
| `step <n> [dt]` | runs `n` `Game_Update` frames back to back; renders only where the game needs it (state change, pending save, deferred GUI reload) |
| `fp-report` | logs the x87 control word and MXCSR of the game thread |
| `world-hash ...` | simulation checkpoints ([world-hashes.md](world-hashes.md)) |

Typical start: `seed 1`, `fixed-dt 0.02`, `boot-gameplay ...`, then advance time with `step` (at 8x and 20 ms one step is
0.16 s of game time; one game hour is 312.5 steps). `step` runs about 2000 steps/s with guests in Debug.

**Keep `dt x speed` at or below about 0.25 s per step.** Above roughly 1 s per step guests visit buildings noticeably less
often (-12 %, -30 % at 4 s), so the simulation no longer matches the game; `step` logs a `WARNING` past the limit.

`click` and `gui-click` wait 50 ms and at least 3 frames before the press, then 100 ms and at least 6 frames before the
release (`wait <ms> <frames>`), so the number of frames during a click still depends on machine speed; deterministic
tests use `move`, `await frames 1`, `down 0`, `await frames 1`, `up 0` instead. A `GuiButton` takes a new press only
about 0.1 s of game time after its last one (it fires 0.05 s after the press and stays pressed 0.05 s longer,
`ButtonTick` in `gui_controls.h`): at `fixed-dt 0.02` leave at least 6 frames between two presses of the same button,
or the second press is lost. The frame minimum of `click` exists for this: with millisecond waits only, a loaded PC ran
1-3 frames per click and `gui_actions` lost a ticker or volume click in parallel runs.

## Uninitialized memory
`seed` and `fixed-dt` are not enough when the simulation reads a field no constructor wrote: the value depends on what the
heap block held before. Symptom: two runs of the same profile give two different world hashes, always the same two, and
the first difference is one object's decision on identical state. The object records (ObjectManager types 1-20) and the Weather are `new T()`:
value-initialized, so no field keeps heap bytes. `guest_construct_fields` (`guests-without-place`, `m_CurID = -1`) and `staff_construct_job_mask` (the mechanic's `m_JobMask`) keep the constructor fixes covered. A headless script list reuses one heap for several
worlds, so such a field shows up there as a script that fails batched and passes alone.

## Rendering must not feed the simulation
`step` renders only at transitions, a normal frame renders every time, and a replay splits its ticks into `step` batches with
a render in between; the headless host never renders. The simulation must therefore not read anything a render writes. Known
case, fixed: the bird sound in `WorldTestState_Update` (`PlayRandomBird`) drew its last `rand()` only when the crowd volume left
room, and the crowd volume follows `MapView::m_guestObjectsDrawn`, which only `MapView_DrawObjects` counts. A render between two
`step` calls thus shifted the random stream (reference `s12_bar`). The bird is now drawn before the crowd check. Symptom of
such a case: the same ticks run as one `step` or as two give different hashes, the first difference being `rand-calls`.
To find one, record both variants with `record ... every 1` around the split, compare the first differing checkpoints with
`world_hash_compare.ps1`, then log the callers of `rand()` around the first differing call count in both runs.

## Floating-point contract
The project compiles with `/arch:AVX2 /fp:precise` (public options of `obl_common`, so they cover the game, the launcher,
the converter and the unit tests): scalar SSE/AVX arithmetic in
`float`/`double`, expression order preserved, no FMA contraction (no `/fp:contract`; Clang and GCC use
`-ffp-contract=off`, and the binaries contain no `vfmadd`). obl-sim and the Linux unit tests build with `-march=haswell
-mfpmath=sse` and give the same hashes. MXCSR is the default control word (round to nearest, no FTZ/DAZ). The game has no x87 code
(the loading bar is C++ with double accumulators, `renderer_draw.cpp`); `tests/fp_environment.txt` checks MXCSR. CRT transcendental functions differ between platforms, so the
simulation takes `acos` and its sine table from `det_math`; `sqrt` is correctly rounded everywhere.

## Checking repeatability
```powershell
./tools/determinism_check.ps1 tests/<script>.txt [-Runs 2]
```
Runs the script several times and compares the `autotest:` lines (without wall-clock values) and every `shot` pixel by
pixel. On a difference the evidence stays in `run/determinism-check/<time>-<script>/` (`runN.log`, normalized `runN.txt`,
`diff.txt`, `shots/`), and the first 40 differences are in the error text.
