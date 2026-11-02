# Unit tests

`obl_unit_tests` tests pure logic (code without the game or the log) in about a second, without starting the
game. Pure logic lives in `src/core/` or in files split out so they compile without the game.

## Running
```powershell
./build.ps1 -Test                                      # Debug build + fast ctest entries
./tools/runtests.ps1 -Filter unit_data*               # installed-data oracles on Release
./build/x64/bin/Debug/obl_unit_tests.exe              # all cases
./build/x64/bin/Debug/obl_unit_tests.exe geometry. -data.   # prefixes: include, or exclude with "-"
```
Exit code: 0 = all passed, 1 = a failure (or the filter selected nothing), 77 = every selected case skipped (ctest
`SKIP_RETURN_CODE`). `runtests.ps1` runs them too through the manifest entries `unit.ctest` and `unit_data*.ctest`
(every ctest entry but `unit` on RelWithDebInfo, [test-runner.md](test-runner.md#game-slots)). Both `build.ps1 -Test`
presets exclude all `unit_data*` entries as well as the replay, bot and chaos labels. The remaining non-corpus data
oracles also exceed the two-minute budget in Debug. Installed-data tests remain registered for explicit runs and the
manifest runner; `ctest --test-dir build/x64 -C RelWithDebInfo -R '^unit_data($|_)'` selects them directly after a Release
build. Build time is separate from the under-two-minute test target.

| ctest | Cases | Needs |
|---|---|---|
| `unit` | everything except `data.` | nothing (synthetic data) |
| `unit_optimized` | construction progress clamp bit semantics on RelWithDebInfo | nothing; manifest selects Release automatically |
| `unit_data` | `data.*` without the two entries below | a prepared `run/`; SKIP without it |
| `unit_data_saves` | the save-corpus cases (`data.convert_save_*`, `_interprets_*`, `_reads_local_save*`, `_writes_local_saves*`): ~25 s Release, over 20 min Debug | as `unit_data`; filter it only for save-format changes |
| `unit_data_maps` | `data.convert_maps_*` (every scenario map): ~80 s Release | as `unit_data`; filter it only for map changes |
| `headless_sim` | `OpenBeach.exe --headless tests/replays/headless_s1.replay`: scenario 1 without window, audio or input; its checkpoint hashes come from the windowed game ([../architecture/headless/index.md](../architecture/headless/index.md)) | a prepared `run/` |
| `headless_save`, `headless_save_reload` | a replay from an `obl.save` headless (`start save`) and the same save loaded after play in one process, with the same hashes | a prepared `run/` |
| `headless_replay_<name>` (label `replay`) | the reference replays of `tests/replays` headless (~110 s in Debug together); the `build.ps1 -Test` presets exclude the label, `runtests.ps1` runs them | a prepared `run/` |
| `headless_bot_<name>` (label `bot`) | the scenario bot runs of `tests/bots` headless ([../architecture/bots/index.md](../architecture/bots/index.md), ~15-20 s each in Debug); excluded from the `build.ps1 -Test` presets like `replay` | a prepared `run/` |
| `standalone_host` | `OpenBeach.exe --no-entry` starts the host and initializes the game | a prepared `run/` |

`data.` cases are oracles on the user's data: they compare our loaders with the installed files (the
executable is SHA-256 checked; another build skips). The game directory comes from `OBL_GAME_DIR` (CMake, default `run/`).
The MP3 scanner joins directories with a portable slash. `mp3info.scan_file_reads_in_chunks` covers a synthetic file
without game data; `data.mp3info_reads_the_soundtrack_tracks` also checks Day1 and Night1 on Linux.

## Adding a test
- File `tests/unit/<area>_test.cpp` with `OBL_TEST_CASE("area.name")` and the macros `OBL_CHECK`, `OBL_CHECK_EQ`, `OBL_REQUIRE`, `OBL_SKIP`
  (`tests/unit/unit_test.h`); add the file and the sources under test to `tests/unit/CMakeLists.txt`.
- A case that needs the user's files gets the `data.` prefix and calls `OBL_SKIP` when they are missing.
- Scratch files and directories come from `tests/unit/temp_dir.h`: `obl::test::TempPath tmp("obl_<area>_<name>")` gives a
  name that no other run picks (parallel runs share the system temp directory) and removes it when the scope
  ends, also when an `OBL_REQUIRE` throws. Never a fixed name under `temp_directory_path()`.
- Store a call result in a named variable before dereferencing it inside `OBL_CHECK_EQ` (a reference bound to a temporary
  dangles; ASan reports `stack-use-after-scope`).

## Linux (GCC and Clang, x86-64)
```bash
tools/unit_linux.sh [gcc|clang|all]           # x86-64, trees in build/linux-<compiler>-m64/
tools/unit_linux.sh gcc-x64|clang-x64         # 64-bit portable part (build/linux-<compiler>-x64/)
tools/unit_sanitize.sh [gcc|clang|all]        # ASan + UBSan, findings in build/linux-asan-<compiler>-m64/findings.txt
tools/unit_coverage.sh                        # GCC line coverage of src/common/ -> coverage.txt / coverage.json
tools/windows_syntax_linux.sh [pattern...]       # Windows-only sources, Clang + x86_64 MinGW-w64 headers
```
Needs cmake, ninja, gcc/g++ and/or clang (Clang sanitizers: `libclang-rt-<N>-dev`; Windows syntax: `g++-mingw-w64-x86-64-posix`). The Linux CMake entry is `tests/unit/linux/CMakeLists.txt`;
new `tests/unit/*_test.cpp`, and `src/core/*.cpp` are picked up by glob, a file from `src/app/` must be added to
`GLUE_SOURCES` there.

`tools/unit_linux.sh gcc -DOBL_UNIT_SDL=OFF` builds without fetching or linking SDL. This excludes the sound-bank,
scripted-sound (which also uses `SoundBank`) and image-comparison tests, plus the sound-bank implementation. Other
pure audio logic tests still run. Plain CI jobs check both configurations; a later local run can restore the default
with `-DOBL_UNIT_SDL=ON` in the same build directory.

To measure the same arithmetic as MSVC the Linux build uses scalar AVX2 math (`-march=haswell -mfpmath=sse`), no
contraction (`-ffp-contract=off`) and handles the 4-byte `wchar_t` (text in game layouts uses `layout::Char16`; never assume
`sizeof(wchar_t) == 2` in logic code).

Sanitizer triage: a bug in a test is fixed in `tests/unit/`; a bug in game logic becomes an issue; a documented
quirk is pinned by the test on values without UB, not silenced.

Game objects in fixtures: a game object's constructor and destructor run the `Construct`/`Destruct` template of its logic
header with the game's Env; a unit test calls the template with a fake Env. The seams (`tests/unit/*_virtuals_seam.cpp`) give
every object an empty destructor, so a fixture can go out of scope without game services.
A fixture that fills an object's bytes to see what `Construct` writes still needs a real vtable,
because UBSan's `vptr` check reads it before any member access. Build fixture objects with
`obl::unit::FillObject` / `StampVtable` (`tests/unit/object_storage.h`) instead of a bare `memset` or a byte buffer
(a fake vptr made the check SEGV in `__dynamic_cast`). `unit_sanitize.sh` limits ASan's quarantine to 64 MB, which keeps a whole run small.
Objects that are members of a `std::string`/`std::vector` (definitions) are value-initialized (`make_unique`),
never `memset`: a memset zeroes the pointers and LeakSanitizer then reports the heap blocks.
