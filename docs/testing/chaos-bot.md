# Chaos bot

The chaos bot has a generator, hard invariants, fresh-process determinism, shrinking and ctest seeds (label `chaos`), plus
counter, object/grid and cash ledger soft findings; the `tricky` probes cover scenery sales, accommodation closure/demolition and construction traps.

Scenario bots ([../architecture/bots/index.md](../architecture/bots/index.md)) prove the campaign can be won; they play well and
stay on the happy path. The bugs found by playing were off it: building rotations 4-7 trap in construction, and selling
the same fountain repeatedly bought daily income. A chaos bot plays badly on purpose and checks invariants instead of a
win, so such bugs show up as a failing seed instead of a forum trick.

Code: `src/game/bot/chaos.h` (generator, run file, rand() jump-ahead; pure), `src/testing/chaos_host.cpp` (headless host),
`tools/chaos_runs.py` (runner, determinism replay, shrinker). Tests: `tests/unit/chaos_test.cpp`,
`tools/tests/test_chaos_runs.py`, run files `tests/chaos/*.chaos`, ctest `headless_chaos_*`.

## What it does
- `ChaosBot` is a bot of the existing API (`WorldView`/`Queries` in, commands out). Its randomness is its own splitmix64
  generator seeded from the run's `seed` (the same number as the game's rand(), but a separate stream), so the bot's
  choices never shift the simulation's draws and a seed gives the same commands on every platform.
- It thinks every 10 ticks: 1-3 commands (sometimes a burst of 3-8 of one kind), weighted over every command of the set: placements of buildings (40 % of them at a footprint the fit query accepts,
  so the world fills up), scenery and surfaces at random tiles, next to and under buildings, at the map edges and far off
  the map; rotations 4-7 and other bad directions; dozer and demolish (also during construction); hire and dismiss,
  wages and shifts including 0, negative, huge and out-of-range hours; send-staff to points and objects; open/close,
  repair/clean calls, prices, beer and show keys (also unknown ones), the typed object controls with odd values;
  pause (resumed soon, also through `resume-game`) and speed levels 0-2 plus invalid ones. Targets are live objects most
  of the time, ids seen earlier (possibly destroyed since) and junk ids (0, all-ones) sometimes.
- Not generated: NaN/infinite floats (the GUI cannot produce them; a finding there would not be a player's bug).
- `WorldView` carries the scenario's scenery, surface, beer and show keys for it (`sceneryKeys`, `surfaceKeys`,
  `beerKeys`, `showKeys`; every definition, available or not).

## Run file
```
OBL-chaos 1
start scenario sandbox1
seed 1
max-ticks 3000
save-at 1500
```
`start scenario <name>` and `max-ticks` are required. `seed` (1), `fixed-dt` (0.02), `save-at` (none: no round trip),
`checkpoint-every` (500), `check-rejected` (2) and `commands <replay>` (a fixed command list relative to the run file,
played instead of the generator: a shrunk finding) are optional. `--headless <file>.chaos` runs it (`OpenBeach.exe`
or the Linux `obl-sim`):
```
obl-sim: chaos start sandbox1 seed=1 fixed-dt=0.02 max-ticks=3000 save-at=1500 commands=random
obl-sim: boot frames=7 ms=210
obl-sim: end ticks=3000 commands=369 applied=60 rejected=309 rejected-checked=... rejected-changed=0 reload=match hash=3ccc6fc103679265 replay=testlogs/chaos/sandbox1.replay hashes=... hash-ms=... ms=... ticks-per-s=...
obl-sim: PASS
```
Exit code 0 = every hard invariant the host checks held, 6 = one failed (a `FAILED` line names it), 5 = the run could
not start. The host writes the run as a replay with checkpoints and section digests to `testlogs/chaos/<run>.replay`
in the game directory.

## Scripted tricky mode
`mode tricky [family]` plays a scripted probe instead of random commands.

## Invariants (the oracle)
Hard failures, checked every run:
- No crash, trap or `FAILED` line; the run reaches `max-ticks` (a tick that leaves gameplay fails).
- Determinism: `tools/chaos_runs.py` plays the recorded replay in a fresh process; every checkpoint must match.
- Save round trip: at `save-at` (or the first later tick without a message balloon: a loaded world starts without one,
  so a balloon scrolling at the save showed the next feedback notice earlier after the reload) the host writes an
  obl.save (`testlogs/chaos/<run>.obl.save`, as `save-obl`), plays on to `max-ticks`, then loads it
  in the same process (as `load-obl-save`). The file holds the fields the old save drops
  ([saves-dropped-state.md](../architecture/data-formats/saves-dropped-state.md)), so a new loss shows as a reload mismatch. The reloaded world gets
  only the rand() state from the harness (`RandStateAfter`: the msvcrt LCG jumped by the counted calls). A run whose
  balloon never clears before `max-ticks` reports `reload=unsaved`. The host then plays the recorded commands of the
  later ticks and compares every checkpoint from the save on. The `rng`
  section (rand calls since the last seed) is left out of that comparison. The first differing checkpoint writes both
  snapshots (`testlogs/chaos/<run>-<tick>-run.tsv` / `-reload.tsv`).
- Rejected commands leave the world hash unchanged: the host hashes before and after a rejected command. A hash costs
  ~50 ms in Debug, so only the first `check-rejected` rejections of each command name are checked.

Soft findings are read-only observations at every checkpoint (including tick 0, the final tick and `save-at`), in the
uninterrupted run only. `checkpoint-every` controls the interval. Each observation prints `FINDING @<tick> ...` and is
written to `testlogs/chaos/<run>-findings.tsv`; the runner's result includes `soft samples=... findings=... file=...`.
Counts are observations, so a persistent condition appears at successive samples. Findings never change the exit code,
world hash, replay, reload oracle or shrink signature. A fresh run replaces its TSV.

Implemented in `src/game/bot/chaos_findings.h`:
- Negative world guest/building counts; per-building guest, male/female/non-serving staff, rental stock (lilos, boards,
  snorkels) and occupied capacity counts. Instance lists use unsigned counts: values beyond their storage capacity
  (including a wrapped negative value) are reported, and scans are bounded to that capacity.
- Grid ids the ObjectManager does not resolve (`unlisted-grid-object`, scenery included); buildings without any tile-list reference.
- Building occupation bit `0x10` outside all live building footprints, and missing occupation bits within a building's
  footprint. Footprint layers 0 and 3, rotation, staggered-grid wrapping and the plain rectangle fallback follow the
  construction rules. Paving, free-water and depth bits are not exclusive building ownership markers.

- Cash ledger: sampled same-day cash movement versus operating income plus booked demolition refunds minus total
  costs. Findings list each delta separately, with construction and scenery costs as diagnostic subsets (never
  subtracted twice). The first sample establishes a baseline; a changed day reports `cash-ledger-reset` and rebases
  because daily counters reset and the history omits refunds. That interval is not checked. Missing players clear the
  baseline. Residuals within one currency unit plus eight float ULPs of balance scale are ignored; nonfinite residuals
  are reported. Observations compare consecutive samples, so a one-time discrepancy is not repeated forever.

Still open: reachable guest/staff tiles, and
references to destroyed objects outside the tile lists. This observer is not a general heap validity checker.

## Findings become tests
`python tools/chaos_runs.py --shrink` cuts a failing run to a minimal command list: ddmin drops chunks of commands while
the run still fails the same way (the same failure kind), then a binary search finds the shortest `max-ticks`. It writes
`run/tmp/chaos/shrunk/<run>-<kind>.replay` (the commands) and `.chaos` (the run that plays them). A finding moves from
there into `tests/replays/` (or `tests/chaos/` when only the harness checks it, such as a save round trip) with its own
manifest entry. A fix of a game bug follows the usual rule: its own test, and a conscious re-record of affected
references in the same commit.

Intermittent failures (uninitialized or freed memory read by the game shows in some processes only): `--runs N` replays
each run N times and prints `distinct-end-hashes=k/N+1` (the run's own hash included), which gives the rate; with
`--shrink` each trial counts as failing when any of N attempts fails the same way (a determinism trial: one run and
max(N, 5) replays). Pick N about three times the observed 1-in-k rate; a trial costs N runs. Without `--runs` a trial is
one run (five replays for determinism), enough for a failure that repeats (a crash, a reload mismatch, a rejected command
that changes the world). Most such findings show the first differing field in the two runs' snapshots, which points
straight at the object that read the garbage.

## Running it
- Per change: `ctest -L chaos` (`headless_chaos_<run>` for every `tests/chaos/*.chaos`, through
  `tools/chaos_runs.py --filter <run>`): a few fixed seeds of a few thousand ticks, ~20 s each in Debug.
  `build.ps1 -Test` leaves the label out, like `bot` and `replay`.
- Occasionally (manual CI run or locally): `python tools/chaos_runs.py --seeds 1 2 3 ... [--ticks 20000] [--shrink]`;
  many seeds and long runs, one line per seed with its failure kind. Not on every push ([../build/ci.md](../build/ci.md)).
