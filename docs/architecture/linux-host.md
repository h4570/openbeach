# Linux host (`obl-sim`)

The Linux build of the headless simulation: one x86-64 executable, `obl-sim`, that runs the same game C++ as the Windows
host in headless mode ([headless/index.md](headless/index.md)) on the game data of a game directory. Both build from the
same sources, and the checkpoint hashes of the reference replays are identical on both hosts. CI's `sim` job runs the L0
entries ([ci.md](../build/ci.md)).

## Build and run
```
tools/obl_sim_linux.sh [-k]          # cmake + ninja into build/linux-sim-x64 (or $OBL_SIM_BUILD); -k: show every compile error
build/linux-sim-x64/bin/obl-sim --game-dir run --headless tests/replays/headless_s1.replay
```
The host builds for x86-64 only; the WebAssembly build ([web-build.md](web-build.md)) covers 32-bit pointers.
The build also produces the portable `obl-convert` CLI. When `run/BeachLife.exe` exists, the script converts its GUI
tables to `run/packs/base/gui/`, the game parameters to `run/packs/base/rules/`, and the catalog, guest sets, texts and
`pack.json` to `run/packs/base/` before returning; conversion failure fails the script. A custom game directory used with
`--game-dir` needs the same `obl-convert gui-tables <BeachLife.exe>` `<game-dir>/packs/base`, `obl-convert game-parameters
<game-dir> <game-dir>/packs/base` and `obl-convert guests <game-dir> <game-dir>/packs/base` preparation
([../build/game-data.md](../build/game-data.md#converted-pack-in-runpacksbase)).
Prerequisites: `cmake`, `ninja`, `clang`; `gdb` helps. From WSL a
build tree on the Linux file system (`OBL_SIM_BUILD=$HOME/obl-sim`) is much faster than one under `/mnt/c`; the game
directory can stay on `/mnt/c`. The log is `<game dir>/openbeach.log`, as on Windows; the `obl-sim:` lines also go
to stdout. Exit codes: the headless run's (0 PASS, 5 unable to proceed or fatal error, 6 mismatch), 2 bad arguments,
222 an unhandled input (`fatal::UnhandledInput`), and a fault (`SIGSEGV`, ...) ends with its signal
after the log names it and the stack. Frames
printed as `obl-sim+<offset>` are static functions that `addr2line -f -C -e obl-sim` resolves.

## Test runner
```
python3 tools/obl_sim_tests.py [--game-dir run] [--no-build | --sim HOST] [--filter PATTERN ...] [--solo] [--list]
```
Builds `obl-sim` (`tools/obl_sim_linux.sh`, into `$OBL_SIM_BUILD` or `build/linux-sim`), then runs in this order, one
`PASS|FAIL <entry> (s) - reason` line each and a table at the end (exit 0 all passed, 1 any failed, 2 bad arguments/no data):
- the headless ctest entries of `tests/unit/CMakeLists.txt` with the same expected output (`headless_sim`, `headless_mismatch`, `headless_refuse`, `headless_save_fixture` then `headless_save`,
  `headless_save_reload`, `headless_biz_map`, `headless_batch`), plus `headless_missing_image` (a linked
  copy of the game directory without `packs/base/images/cursor.png` must stop with a `FatalError` naming it, exit 5,
  in both stdout and the log; the Windows manifest runs the same check through ctest);
- `replay_<name>`: every reference replay of `tests/replays` (all but the `headless_*` and `mismatch` fixtures);
- the `.golden` entries of the default run (`golden_hash_s1/s3/s12`): the `{scenario}` template instantiated under
  `<game dir>/testlogs/linux/scripts`, its `world-hash` lines compared with `tests/golden-hashes/<name>.tsv`;
- the `@headless` scripts of `tests/manifest.txt` but those with a `# @windows-only` header (`crash_handled.txt`: SEH): 12 per process (script lists) as `runtests.ps1` does, scripts with a
  solo header (`@gameini`, `@env`, `@check`, ...) alone; a script that did not end in its batch runs again alone.

As `autotest.ps1`, every run starts from `BeachLife.ini` with 800x600 and no `[Dialog]` plus the script's `@gameini`, and
gets the ini and `SaveGames` back afterwards; `@check` oracles run after a pass. Logs: `<game dir>/testlogs/linux/`. It
uses the game directory alone: do not run it while a Windows test run uses the same `run/` (one `openbeach.log`).
A game directory on `/mnt/c` is much slower than one on the Linux file system: each world start reads the map through the
Windows file share. `/mnt/c` matches names ignoring case, a Linux file system does not (the installation has `Data`, `Photos`): test it
there before trusting a path our code builds itself (`os::ResolvePath`).
From Windows: `wsl -d Ubuntu -- bash -c 'cd /mnt/c/<checkout> && OBL_SIM_BUILD=$HOME/obl-sim python3 tools/obl_sim_tests.py'`.
`--jobs N` (default `nproc` on Linux, 1 on Windows; `tools/bot_runs.py` has the same option) runs N processes at once, each
in a worker game directory of symlinks with private copies of the files a run writes (`tools/sim_workers.py`); fixed
headless entries that share fixtures run in one job.
`--sim <host>` runs the same entries with a host already built and skips the build, also on Windows
(`python tools/obl_sim_tests.py --sim build/x64/bin/Debug/OpenBeach.exe --filter "replay_*"`); logs still go to
`testlogs/linux/`.

## Toolchain and ABI
- **Clang only** (GCC has no MS inline asm or `__try`), x86-64, C++20, `-fms-extensions -fasm-blocks`, `-O2`. Floating
  point is scalar AVX2 like the Windows `/arch:AVX2` build (`-march=haswell -mfpmath=sse -ffp-contract=off`). `-mlong-double-64`: an 8-byte `long double` like MSVC's.
- **x87 precision:** none. The loading bar is C++ on doubles, so the host sets no x87 control word; Linux's default 64-bit
  precision control is never used for float results.
- **Game text is `char16_t`.** The game's text is UTF-16; shared code holds it as `Char16`/`Text16`
  (`src/core/char16.h`, `char16_t` on every host) with `u""` literals, and `wchar_t` is the platform's own (4 bytes here).
  The C library's wide functions take `wchar_t`, so game text goes through `core/text16.h` (copy, compare, find; format
  and parse via the C runtime on Windows, `core/wide_format.h` elsewhere). Windows API calls cast to `wchar_t` at
  the call (`reinterpret_cast<const wchar_t*>`); the launcher ABI (`host::StandaloneInit::gameDir`) stays `wchar_t`.
  Paths go through `WidePath()` (`src/core/wide_path.h`: UTF-8 and `/` on Linux), `Utf8Path()` for `os::GetEnv` values,
  `WideUtf8()` for logs; `%ls` in a narrow format expects `wchar_t`, so log game text as `%s` + `WideUtf8`.
- **MSVC extras** come from the force-included `src/host_linux/msvc_compat.h` (this build only): `_stricmp`/`_strnicmp`,
  `_strupr`, `strcpy_s`, `_ReturnAddress`, msvcrt's `_O_*` values. `src/host_linux/win_types/` holds the type-only
  `windows.h` (handles, `RECT`, `BITMAPINFO`, `LARGE_INTEGER`, ...; no function), and `intrin.h`: a source that
  calls a Windows function does not compile and gets a portable path or an `#ifdef _WIN32` branch.

## Process layout
- **One executable, no host process**, linked non-PIE against the shared glibc. Every `src/app`, `src/testing` and `src/platform` source is compiled except the `WINDOWS_ONLY` list.
- **SDL3** links statically with dummy drivers only (`SDL_UNIX_CONSOLE_BUILD`); headless never initializes a subsystem.
- **`main`** (`src/host_linux/main.cpp`): arguments, `setenv` of `OBL_HEADLESS` (and `OBL_SCRIPT` for anything but a
  `.replay`), `chdir` to the game directory, the log, fault handlers, `InitGame` (config, packs, the module list),
  `RunStartup` + `SnapshotStartupData`, then `RunHeadlessGame` (`platform/headless_main.cpp`, shared with the
  Windows WinMain: `GameParameters`, `Game_Construct`, `test::RunHeadless`, teardown) and the `_onexit` list.

## What the Windows-only pieces become
| Windows piece | Linux `obl-sim` |
|---|---|
| `OpenBeach.exe` + `OpenGame` | `main` + `InitGame` (above); `game_start.cpp` keeps `OpenGame` under `_WIN32` |
| window, presentation, input devices, intro movie player, frame limiter | left out of `obl-sim` (`WINDOWED_MODULES` in `src/host_linux/CMakeLists.txt`; the windowed `OpenBeach`, `-DOBL_LINUX_WINDOWED=ON`, builds them with `OBL_WINDOWED_HOST`); `InitGame` skips their installers (`OBL_WINDOWS_ONLY`, `src/platform/windows_only.h`); `headless_modules.cpp` stubs their other entries |
| Win32 test diagnostics, Win32 hidden mode | left out (`WINDOWS_ONLY`); `test_tool_stubs.cpp` has the test commands that answer "not mine"; the windowed host's hidden mode is `hidden_window_sdl.cpp` (SDL offscreen video driver) |
| the Input object's SDL backend (`input.cpp`, `cursor.cpp`) | none: the shared object (`input_ops.cpp`) runs with its null backend, `headless_input.cpp` only says so ([input.md](input.md)) |
| the intro movie (`intro_movie.cpp`) | `headless_modules.cpp`: the `Movie` object and a skipped `Play` (headless sets `PlayMovie=0`) |
| `platform::RedirectImport` | returns false (no Windows imports) |
| `fatal::UnhandledInput`, `fatal::DescribeAddress` | `_exit(222)`; names through `dladdr` and `__cxa_demangle` |
| crash diagnostics (`platform/test_tools/crash.cpp`) | `main`'s `SIGSEGV`/`SIGBUS`/`SIGFPE`/`SIGILL`/`SIGABRT` handler: the log names the fault, its address and the stack (`backtrace`) |
| monitor size (`platform/window_placement.cpp`) | a 1080p desktop: the resolution list is built for 1920x1080 |
| game RNG | shared native `GameRng` (`src/game/game/rng.h`), bit-identical on both hosts |
| text formatting/parsing and process lifetime | `src/platform/runtime.cpp`: `wide_format`, `std::exit`, logged quit requests |

The native RNG replaces the CRT table on both hosts. Its source is thread-local and separate from the test tape;
recording advances it, replay only returns recorded values. Paths use `os::OpenFile`/`os::ResolvePath`. Converted
sprite banks keep no header stream or descriptor open.

Linux text formatting uses `src/core/wide_format.h` with msvcrt semantics (`%s` in a wide format is a wide string,
`%S` narrow, three-digit exponents, `%p` as `%08X`). `wide_format_test.cpp` compares these conventions with the real
UCRT and msvcrt on Windows. Windows platform formatting still calls msvcrt to retain its rounding.

Heap addresses and fresh allocation contents differ between hosts; the world hash sees no pointers. A field never
initialized by the game can still cause Linux-only divergence. For example, `Building::m_rScrExtents`, set only
when drawing the building, would make a headless demolition queue allocate an enormous block rebuild; the constructor
clears it and the queue clips to the block grid. Check such fields when a run matched earlier checkpoints.

## Image portability
The cursor, screens and haze load converted PNGs from the selected pack. Removing an installed `Data/cursor.tga`
therefore does not test a missing runtime image: `headless_missing_image` removes the converted cursor PNG. Its
startup failure reaches the shared `FatalError` handler (`game/diagnostics.cpp`), which reports the file and exits 5
before the headless tick loop; no Linux host exit override is needed.

TIFF colour-map channels convert from 16 bits to 8 with integer division by 257, which is independent of the converter's
floating-point flags. Verify against packs freshly converted on Linux, not a copy of Windows-generated packs.

## Game paths
The game's paths mix case and `\` (`savegames\save3.dat` for `SaveGames`, `data\sounds\blsnd.hdr` for `Data/Sounds`).
`os::ResolvePath` (`src/core/os.h`) is the file-open layer: unchanged on Windows; on Linux `/` separators and every
component that does not exist as written is the entry of its directory with the same name ignoring ASCII case. It is
applied by `os::ListDirectory`/`CreateDirectoryIfMissing`, and `os::OpenFile` (fopen
through it), which every game file our own code opens uses (images, sound banks, saves, progress, grids, water, the temp
map, sprite files). New code opening a game path uses `os::OpenFile`, or `os::ResolvePath` for an fstream.

## Not yet on Linux
- Text encoding of the renderer's fonts uses 1252 for every font (Polish letters 1252 lacks become `?`; nothing is drawn).
- `ShowErrorAndExit` logs instead of a message box; memory-mapped files use `mmap`.
