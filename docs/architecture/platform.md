# Platform layer (`src/platform/`)

`src/platform/` holds the OS and backend code of the game: SDL windowing and presentation, input, the intro movie,
sound device lifecycle and the music folder scan. It is the top of the dependency hierarchy: `platform -> game -> core`.

## Rules (`tools/check_layers.py`)
- Windows and SDL headers are allowed (unlike `src/core/` and `src/game/`).
- `src/core/` and `src/game/` never include `src/platform/`.

## Build placement
The files link into the `openbeach` object library (linked into `OpenBeach.exe`): a `target_sources(openbeach ...)` block at the end of
`src/app/CMakeLists.txt`, because they call game-side modules (settings, cursor, display geometry, the test tools).
They are included as `"platform/<file>.h"` (`src/` is on the include path). The Linux `obl-sim` globs
`src/platform/*.cpp` with the game sources; `input.cpp`, `intro_movie.cpp` and `sdl_present.cpp` are on its
Windows-only list (`src/host_linux/CMakeLists.txt`).

## Contents
| File | Role |
|------|------|
| `input.*` | SDL input backend ([input.md](input.md)) |
| `sdl_present.*` | SDL window, presentation and the frame present path |
| `intro_movie.*` | MPEG-1 intro playback (`pl_mpeg`) |
| `sound_lifecycle.*` | sound device startup/shutdown |
| `playlist_directory_scan.*`, `playlist_id3.*` | music folder scan and ID3 tags |
| `cursor.*`, `input_mapping.*`, `frame_limiter.*`, `instance_lock.*` | local cursor, key mapping, FPS limiter on SDL time, one game per game directory (lock file) |
| `platform_main.*` | process start-up and the windowed main loop |
| `renderer_dirty.h`, `renderer_jpeg.*` | pure renderer helpers (`core/rect.h` `screen::Rect`) |
| `renderer_display.*`, `renderer_surface.cpp`, `renderer_backbits.h` | display and surface renderer parts |
| `renderer_capture.cpp`, `renderer_draw.cpp`, `renderer_helpers.cpp`, `renderer_lifecycle.cpp` | screenshots/photos, image and loading-bar drawing, the small operations, construct/destruct |
| `hidden_window.*` | hidden test runs |
| `import_redirect.*` | `platform::RedirectImport` |

The renderer files implement the `renderer::` facade (`game/ui/renderer_api.h`) directly and need no start-up call.

## Interfaces declared by the game
A game module that needs an OS service includes a declaration-only header under `src/game/`; the platform file implements it
(Windows code and a Linux fallback in one `.cpp`, which the `obl-sim` glob picks up). Handles cross the boundary as opaque
`void*` (window, module instance), interpreted only here.

| Interface (`src/game/`) | Implementation (`src/platform/`) | Service |
|------|------|------|
| `ui/renderer_api.h` | `renderer_*` | renderer backend calls |
| `ui/memory_mapping.h` | `memory_mapping.cpp` | page-file mapping of the map view block cache; Linux: none (headless only) |
| `game/local_time.h` | `runtime.cpp` | the 32-bit msvcrt `localtime` of the save-slot dates |
| `game/diagnostics.h` (`RaiseAccessViolation`) | `diagnostics.cpp` | the null-node crash of the event list |
| `ui/input.h` | `input.cpp` (test injection) | the `Input` object API with opaque handles; `platform/input.h` keeps only the message pump |

The playlist scan does not follow `.lnk` shortcuts: resolving them needs the Windows shell.
