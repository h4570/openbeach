# Scenario bots: metrics, tolerance bands, runner and diagnostics

## Metrics and tolerance bands
Winning is not enough: a bot that wins through a bug, an exploit or a drifted economy must fail too. The host prints a
metrics sample at tick 0 and at the first tick of every new game hour (read-only; the world hash is unchanged):
```
obl-sim: sample tick=1250 day=0 hour=13 cash=1951 guests=94 rating=51
```
`rating` is the resort rating, the guests' mean happiness in percent, so it is the guest satisfaction. Each sample line also ends with `unhappy-leavers=<n>` (the world's unhappy departures, the loss counter of scenarios 9 and 10;
the band parser ignores it) and a run prints `leavers unhappy=<n> departed=<n>` before PASS/FAILED. Samples are keyed
by game time, not ticks, so the same hour compares across seeds even when a purchase shifts.

`tests/bots/<run>.bands.json` (committed, numbers only) holds what the recorded seeds (1, 2, 3) reached: the ticks to the
outcome, cash/guests/rating at the end and, for every game hour all seeds reached, the `[min, max]` of cash, guests and
rating. A run passes when it reaches the run file's outcome and every value lies in `[min - pad, max + pad]` with
`pad = max(floor, rel * max(|min|, |max|), (max - min) / 2)`; `TOLERANCE` in `tools/bot_runs.py` sets `rel`/`floor`
(ticks 10 %/300, cash 15 %/500, guests 10 %/10, rating 0/8 points). The policy lives in the tool, the files hold only
measurements, so a policy change needs no re-recording. Hours a run never reached (it ended earlier) are skipped; the
ticks band judges how much earlier. A run file without bands fails.

Re-record after a deliberate behavior change or a bot change, in the same commit, with the reason in the body (as replay
references): `python tools/bot_runs.py --record --filter <run>`; it writes the file only when every seed reached the
expected outcome. A new bot records its bands before its first commit.

## Runner and evidence
`python tools/bot_runs.py [--filter build_tree_s1] [--seeds 4 5 | --run-seed] [--record]` runs the run files through the
newest local host (Windows) or `obl-sim` (Linux), each with the seeds of its bands (another seed is a copy of the run
file under `run/tmp/bot-seeds/`), prints one line per run plus the first values outside the bands, and writes
`run/tmp/bots.json` with this evidence contract: a catalog scenario is
completed when one run file that expects `won` won it on every seed inside its bands. `runs` adds each run's file, seed,
verdict, violations, end values and hourly samples, for balance reports. The evidence names the commit; a changed
working tree writes `<commit>-dirty`. The catalog is `tests/bots/scenarios.json` (the twelve campaign scenarios, ids only).


## Diagnostics

Host diagnostics for writing a bot (read-only, off by default): `OBL_BOT_TYPES=1` prints the scenario's building types, their
capacity and commodity families and the scenery keys at boot; `OBL_BOT_OPINIONS=1` adds, to every hourly sample, the guests'
mean opinions and tolerances, the finished buildings' repair state/closed flags and the power figures, guests without a bed
and leaving early, an `early-leaver` line per guest that turned to leave (happiness, bed, opinions), the guests with a strong
want per desire (`wants`) and every drop of a guest's variety opinion since the last sample, keyed by the desire, the building it
headed for, `/queue` when crowding fell too and its newest thought (`variety-drops`), plus the script's current party
count. The sample line also
carries the day's income, safety opinion and income/cost breakdown.
