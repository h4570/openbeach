# CI L0 tests and compile time

The `sim` step `L0 tests without game data` runs `tools/ci/l0_sim_tests.sh` before the archive is fetched, so it also
runs on forks without the secrets. The script builds obl-sim (without conversion), makes `build/l0-run/` holding only
`packs/l0/` (a copy of the committed `data/packs/l0/`), an empty `Data/` and a `BeachLife.ini` with only the settings
version (without it the game ignores the scripts' `@gameini` values), and runs `obl_sim_tests.py --filter l0_boot
l0_gameplay l0_guests "l0_bot_*"` on it; the scripts select the pack with `@env OBL_PACK=l0`, the bot runs of
`tests/bots/l0/` (entries `l0_bot_<run>`) get it from the runner. A loader that reaches for an imported file
stops the run (FatalError), so a pass proves the L0 scenario needs no imported data. The bot run `build_tree` wins the L0
goal (every offered building finished before 17:00 on day 1) with the `build-tree-s1` bot; its tolerance bands
(`tests/bots/l0/build_tree.bands.json`) are checked by `tools/bot_runs.py` in the `Scenario bots` step and by the Windows
ctest `headless_bot_l0_build_tree` (the runner sets `OBL_PACK=l0`). Here the bot is checked by outcome only. Local check in WSL (warm build): 4/4 passed, about 3 s of test time. Run it with a native Linux build and game directory:
`OBL_SIM_BUILD=/tmp/obl-sim OBL_L0_GAME_DIR=/tmp/obl-l0run bash tools/ci/l0_sim_tests.sh` (an LF copy of the scripts
when the checkout is on `/mnt/c`, [unit-tests.md](../testing/unit-tests.md)).

### Compile time in the L0 step
Run [40170297834](https://github.com/h4570/beach-life-re/actions/runs/40170297834) spent 16 min 3 s here:
39 s configuring, 15 min 23 s building 683 Ninja edges, and 1 s running all six L0 entries. The later `Build obl-sim`
reported `ninja: no work to do`; its 4 min 46 s was conversion after a converted-pack cache miss. This was a compile
regression, not slower L0 gameplay or a second cold configure.

The compiler cache restored `ccache-sim-b2e9dfd...` from run 40150907838. Its counters were restored too: the printed
438/1870 hits (23.42%) are cumulative. Subtracting the earlier run's 319/1402 gives **119 hits and 349 misses out of
468 cacheable compilations (25.43% hits)** in this run. Another 210 calls were uncacheable: a local verbose reproduction
identified all 210 as SDL's precompiled headers rejected by ccache. Between those revisions
510 files changed, including shared headers, invalidating much of the optimized host. The final cache occupied only
0.3 of its 0.4 GB allowance; these statistics do not establish eviction as the cause. Enlarging it alone would not fix
header invalidation.

The CI sim builds use `-DOBL_SIM_FAST_BUILD=ON`: the host uses `-O1` and Debug emits `-gline-tables-only`, retaining
source locations and exported crash-report symbols. Ordinary local builds retain `-O2 -g`; the converter retains
`-O2` in both modes so a cold pack conversion stays fast. Each target precompiles `nlohmann/json.hpp` (and its standard
library dependencies) with its own ABI flags; no game header enters the PCH. Clang uses `-fno-pch-timestamp`, and the
CI sim job sets `CCACHE_SLOPPINESS=pch_defines,time_macros` as required by ccache for PCH, including SDL's existing
PCH. Informational compile timestamps may be reused from cache; bot evidence still names the Git revision.
This makes SDL cacheable and avoids repeatedly
parsing the same library headers after game-header changes. AVX2, floating-point contraction and fast-math settings
are unchanged. The wrapper prints configure/build seconds separately. CI resets ccache counters after restoration and
prints verbose statistics immediately after L0 and at job end, so the next push can distinguish misses, uncacheable
calls and cache cleanups without subtracting a previous run.

Local WSL comparison (Clang, i386, eight build workers, same sources and dependencies, cold object caches):

| Mode | Compiler user CPU | Cacheable calls |
|---|---:|---:|
| ordinary `-O2 -g` | 1744 s | 468/678; 210 PCH calls rejected |
| `-O1`, line tables | 1177 s | 468/678; 210 PCH calls rejected |
| fast mode with library PCH and PCH-compatible ccache | 843 s | 680/680 |

A subsequent clean rebuild with the CI 400 MB cache limit hit all 680/680 calls (zero misses), rebuilding and linking
in 23 s with eight workers; cache occupancy was 58%.

The fast build passes all six L0 entries, all 244 headless entries and all 14 first-seed bot runs (12/12 campaign
scenarios completed); every bot end hash matches the ordinary `-O2` build. `-O1` trades runtime for compile time:
headless user CPU increased from 264 to 411 s locally. Warm compiler/pack caches should put the hosted job around
9-10 minutes after allowing for that runtime cost, but **under 15 minutes remains unconfirmed on GitHub**. Cold header
rebuilds need measurement too; the local cold-compile CPU reduction cannot be applied directly to a hosted build
with a different mix of cache hits. A cold converted-pack cache adds conversion time (4 min 46 s in the cited run),
including the first run after changing the converter build configuration. Confirm on the next planned push to `main`;
no extra manual CI run is needed.

