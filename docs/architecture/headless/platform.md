# Headless platform behavior

[Headless overview and invocation](index.md). Linux host details: [linux-host.md](../linux-host.md).

## Headless switches in the game code
`config::Get().headless` (from `OBL_HEADLESS`) is read where the game would touch a device:

| Place | Headless behavior |
|---|---|
| `RunWinMain` | no SDL window, COM or message loop; `Game_Construct(instance, nullptr)` |
| `Game_Construct` | `bSOUND_ENABLED` forced off: `Sound` and `LoopSounds` are built disabled, SDL audio never starts |
| `Renderer` construct, `CreateDisplay` | no `GetDC`, no window placement; the screen rectangle is `0,0,800,600`; the memory surfaces exist as usual |
| `RegisterInput` (`input_ops.cpp`) | the Input object runs with no device backend, as on Linux: no SDL events or system cursor; every frame reads as no key, button or cursor ([input.md](../input.md)) |
| `Renderer_Present` | no present; fades still step |
| `BuildGdiFont` (`font_construct.cpp`) | the debug font of `Game_Construct` is not rasterized with GDI: blank atlas, zero glyph rectangles, no kerning |
| `BuildTempMapImpl`, `MapBlockView` (`map_view_blocks.cpp`) | no block cache or pagefile mapping (the GUI cache is also file-free); every block view is one shared scratch buffer, so the code that fills or paints blocks (block loads, background restores, sprite baking, water) runs unchanged on pixels nobody reads |
| `MapCache` (`map_cache.cpp`) | the scenario's `.biz` map file is never opened or decoded: the object is zeroed, the four-slot cache keeps its round robin over zero planes, so block loads and restores copy zeros. The CI game data has no `.biz` files |
| `InitGame` (`dllmain.cpp`) | the log's debugger sink (`OutputDebugStringA`) is removed after `config::Load` |

The renderer object, its memory surfaces, the GUI manager, cursor and fonts are still built: the GUI and the world refer
to them. `sdl-subsystems=0` in the end line confirms that no SDL subsystem was initialized. The block pixels do not feed
the simulation: `headless_s1.replay` and the reference replays recorded in the windowed game (`s1_commands`, `s3_resort`,
`s12_staff`, `s1_surfaces`, `s1_wages`, `gui_controls`: scenery placement, demolition, staff, surfaces, wages, GUI-wired
commands) match every windowed checkpoint headless. `ctest headless_replay_<name>` runs each (label `replay`, ~110 s in
Debug together, so the `build.ps1 -Test` presets leave them out; manifest entries `headless_replay_*.ctest`).

## Platform calls
**Window, input, audio, COM, DirectX, GDI: none.** No `user32`, `gdi32`, `ole32`, `winmm`, `ddraw`, `dinput8`, `dsound`
call and no SDL subsystem.

**Our code calling Windows directly: only the C++ WinMain host**, which the Linux `obl-sim` replaces with its own `main`.

**Behind portable code** (`src/core`, each with a unit test; Windows-only code there stays behind `#ifdef _WIN32`):
- Clock (`os.h`): the game clock (`ClockConstruct`, `UpdateClock` in `game.cpp`) reads `os::TickMs()` (`steady_clock`, wrapping 32-bit
  milliseconds) instead of `GetTickCount`; only differences matter, and `fixed-dt` replaces the delta anyway.
- Code pages (`code_page.h`): the game's narrow strings convert as Windows-1252 on every platform instead of `CP_ACP`,
  byte-transparent, so names read from the file system keep their bytes through
  the wide text (`Scan` widens scenario directories, `NativeEnter` narrows the `.pis` path back). Paths our own code builds
  from wide text are UTF-8 (`Utf16ToUtf8`). The renderer's font re-encoding (`game/ui/text_encoding.cpp`) uses the
  1250/1252 tables of `core/code_pages.h` on both hosts. Still Windows: `ShowErrorAndExit`/`ShowNotice` (`platform/diagnostics.cpp`), the host's own strings.
- Environment (`os.h`): `os::GetEnv`/`HasEnv` (UTF-8, the C runtime's copy taken at start-up, so the launcher's
  variables are seen) in `config.cpp` and the sound teardown reports; the platform layer (cursor, input, frame limiter,
  intro movie, hidden window) and the test interpreter still use Windows.
- Directories (`os.h`): `os::ListDirectory` (`std::filesystem`, sorted by name ignoring ASCII case, the NTFS order the
  unstable save-slot time sort relies on for ties) replaces `_findfirst32` in the scenario `Scan` and the save-slot scan
  (`save*.dat` through `os::MatchesPattern`); `EnsureDirectory` (`photos`, `SaveGames`) calls `os::CreateDirectoryIfMissing`.
  The game's paths mix case and `\` (`savegames\save3.dat` next to `SaveGames`): `os::ResolvePath`/`os::OpenFile` resolve
  them case-insensitively with `/` on Linux at the file-open layer ([linux-host.md](../linux-host.md), "Game paths").
- Ini files (`ini_file.h`): the profile API's measured rules (first section of a name only, `;` comments, one pair of outer
  quotes stripped, values replaced in place, new entries after the section's leading run of entries, `\r\n` for new lines
  and an unterminated last line); `ini_file_test.cpp` also runs every case through `Get`/`WritePrivateProfileStringA` on
  Windows. Used by `IniEnv`/`Settings_Construct` (`BeachLife.ini`, path from `os::CurrentDirectory`) and `config.cpp`.
- Log (`core/log.cpp`): an unbuffered C runtime file, `std::chrono` local time and the C++ thread id (the OS id on Windows); the
  debugger output is a sink. `EmergencyWrite` (crash handlers: no heap, no CRT locks) goes through `log::EmergencyBackend`,
  which the host installs before `log::Open` (`platform/log_emergency.cpp`: duplicated native handle, OS clock).

**C runtime internals (portable through the Linux C/C++ library, no action):** `HeapAlloc`/`HeapFree` (malloc, the debug CRT
adds `HeapValidate`), critical sections, SRW locks and `WakeAllConditionVariable` (CRT locks, `std::mutex`, static-init
guards), `CreateFileW`/`ReadFile`/`WriteFile`/`SetFilePointerEx`/`GetFileType`/`CloseHandle` (stdio and fstreams),
`FindFirstFileExW`/`GetFileAttributesExW`/`AreFileApisANSI` (`std::filesystem`), environment strings, time zone and
file-time conversion, `QueryPerformanceCounter` (`std::chrono`), `FlsGetValue`/`SetLastError` (vcruntime per-thread data),
`__acrt_*` code-page conversions. The CRT locks are the hottest calls (3.6 million pairs in 600 ticks in Debug).

**Not visible to the trace:** Windows `platform/runtime.cpp` resolves msvcrt text formatting, parsing and `exit`
by name to preserve existing rounding and static-destructor registration. RNG is native (`game/game/rng.h`),
allocation uses C++ operators, and file I/O uses native streams. `StartupCrt` of the C++ entry and the host process
also run before the trace, as does `OBL_Init`'s setup (`VirtualProtect`, `dbghelp`).

**Still built in headless (memory only, no device):** the `Renderer` object and its surfaces, the GUI manager with its
scripts and images, the software cursor, the haze images and the map view's block table. The Linux host can drop them once
the simulation stops reaching into them.
