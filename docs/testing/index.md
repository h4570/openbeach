# Testing

The test infrastructure: unit tests, game test scripts, command replays, world hashes, golden images, the headless
simulation and the bots.

| Topic | Summary |
|---|---|
| [unit-tests.md](unit-tests.md) | `obl_unit_tests` (pure logic, no game, about a second), `data.` cases on the user's `run/`, Linux x86-64 builds with GCC/Clang, coverage and ASan/UBSan scripts, and how to add a test. |
| [game-tests.md](game-tests.md) | Scripted tests in the hidden game or the headless host (`-Headless`): `autotest.ps1`, script header directives, the fast boot/await/expect flow, world fixtures, crash and hang diagnostics. |
| [test-runner.md](test-runner.md) | `runtests.ps1` and `tests/manifest.txt`: parallel game slots, ini and save hygiene, run levels, per-entry configuration, scenario templates, `.golden` virtual entries, `@headless` and `@hidden` entries. |
| [determinism.md](determinism.md) | How scripted runs become repeatable: `seed`, `fixed-dt`, `step`, the AVX2 floating-point contract and `determinism_check.ps1`. |
| [world-hashes.md](world-hashes.md) | `world-hash` checkpoints of the simulation state, the TSV snapshot format, per-section digests, comparing two runs and the three retained cash/weather golden fixtures. |
| [replays.md](replays.md) | Command replays: the `record`/`replay` directives on the tick listener, checkpoint comparison and diagnosis, the reference replays in `tests/replays/` and how to (re-)record them with `tools/replay.ps1`. |
| [call-counters.md](call-counters.md) | `CallCounter`: `Count`/`Record`, `call-report`, `call-drive`, direct failures for golden/model checks, and counter maintenance. |
| [chaos-bot.md](chaos-bot.md) | The seeded chaos bot: valid and invalid commands, `.chaos` run files, hard invariants, counter and object/grid soft findings, cash ledger, shrinking (`tools/chaos_runs.py`) and ctest label `chaos`. Explains how findings become regression tests and how to run the bot. |
| [linux-windowed.md](linux-windowed.md) | The windowed game scripts on the Linux windowed `OpenBeach` (`obl_sim_tests.py --windowed`, SDL offscreen video, dummy audio): running them from WSL on a Linux-side game copy, the host differences the first runs fixed, and why the playlist count differed. |
| [golden-images.md](golden-images.md) | Pixel-exact screenshot references: the comparison metric, `obl_imgdiff`, `expect-shot`, recording references and making frames deterministic. |
