# WebAssembly build

The unit tests and the headless `obl-sim` built with Emscripten and run under Node, without a window and without any
imported game data. The browser game is [browser-build.md](browser-build.md).

All synthetic unit tests pass under Node (`obl_unit_tests -data.`), and `obl-sim` on the own L0 pack (`OBL_PACK=l0`,
`data/packs/l0/`) gives world hashes identical to the native x64 `obl-sim`.

## Build and run
`tools/wasm_linux.sh` is the one entry point (CI job `wasm`, `tools/linux_check.ps1` stage `wasm`): it sources an emsdk when
`emcc` is not on `PATH`, builds `obl_unit_tests` (RelWithDebInfo) and `obl-sim` (Release, target only) under
`$OBL_WASM_BUILD`, runs the synthetic cases, writes a `node` wrapper as the `--sim` host and runs the L0 scripts on a fresh
own-pack game directory. With `OBL_WASM_GAME` (a game directory holding a converted `packs/base`) it also runs `data.` and,
through `tools/obl_sim_tests.py`, the golden-hash entries, `headless_sim` and the `replay_*` entries against the native x64
references. ccache works with `emcc` through the usual CMake launcher variables. The unit tree compiles `OBL_GAME_DIR` in, so
it is configured with the game directory from the start.

By hand, with Emscripten **6.0.13** and CMake through `emcmake`:
- Unit tests: `emcmake cmake -S tests/unit/linux -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`, build, then from the
  checkout root `node <dir>/bin/obl_unit_tests.js -data.`.
- `obl-sim`: `emcmake cmake -S src/host_linux -B <dir> -G Ninja`, build, then
  `OBL_PACK=l0 node <dir>/bin/obl-sim.js --game-dir <dir with packs/l0, data/ and BeachLife.ini> --headless <script>`.
  The runner takes a two-line shell wrapper (`exec node .../obl-sim.js "$@"`) as `--sim`; it insists on a `data/` directory
  in the game directory, an empty one is enough for L0.

SDL3 (dummy drivers, surfaces and WAV/PNG loaders only) builds for Emscripten unchanged. A Release `obl-sim.wasm` is 4.8 MB
plus a 164 KB JavaScript loader; Debug builds are over 100 MB (DWARF).

## Flags and shims
Each of these is behind `EMSCRIPTEN` / `__EMSCRIPTEN__`, and the native builds are unaffected:
- **Compiler flags.** `-march=haswell -mfpmath=sse -mlong-double-64` and `-mstackrealign -malign-double` are x86 options and
  are left out (wasm float math is scalar IEEE binary32/64 without extended precision or FMA contraction, and wasm32 aligns
  `double`/`int64_t` to 8); `-ffp-contract=off -fno-fast-math` stay. `obl-sim`'s `-no-pie -rdynamic` are left out as well.
- **C++ exceptions.** Both targets build and link with `-fwasm-exceptions` (native wasm exception handling, Node 17+ and any
  current browser): `nlohmann::json` throws and the loaders catch, and Emscripten's default would turn a `throw` into an abort.
- **File system.** `-sNODERAWFS=1` lets Node read the repository and the game directory directly; plus
  `-sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB` (the default 64 KB stack is too small for the deep game call chains).
- **`__rdtsc`** (`ProfileTimer` in `game.cpp`, through `win_types/intrin.h`): wasm has no cycle counter; the shim returns
  `steady_clock` ticks, enough for the profile timers.
- **`_mm_getcsr`** (`fp-report` in `testing/determinism.cpp`): wasm has no MXCSR; the command reports the default control
  word, which is what wasm always does (round to nearest, all exceptions masked, no FTZ/DAZ).
- **`execinfo.h`, `ucontext.h`, signals** (`host_linux/main.cpp`'s fault handler): wasm has no signals; a trap ends the run
  with Node's JavaScript stack trace, which names our functions. `InitFaultHandlers` is empty there.

## Threads
The browser import (`importer/web/`) gets its parallelism without threads: helper Web Workers, each with its own wasm instance,
talk only by `postMessage` (the page is not cross-origin isolated, so no `SharedArrayBuffer`). `node_check.cjs --game <dir>
--workers <n>` runs the same protocol under Node with `worker_threads`, requires the files of a single worker, and prints both
timings; its synthetic run (CI) checks `shard.js` and a sharded import of a fake copy that must end like a single worker's.

The game creates no thread; `std::thread` appears only in Windows test tools. The `std::mutex`es (`core/log.cpp`,
`game/ui/sound_bank.cpp`) and `std::atomic`s compile to no-ops without `-pthread`, so no `SharedArrayBuffer` is needed.

## 32-bit coverage
wasm32 is the only 32-bit build:

| | wasm32 | x86-64 |
|---|---|---|
| `void*`, `size_t`, `long` | 4 | 8 |
| `alignof(double)`, `alignof(long long)` | 8 | 8 |
| `alignof(max_align_t)` | 8 | 16 |
| `wchar_t` | 4 | 4 |
| `long double` | 16 (soft binary128) | 16 (x87) |
| plain `char` | signed | signed |

Floating point: wasm has no x87 and no FMA contraction by itself, and its
results equal the native AVX2 x64 results on every hash compared.
