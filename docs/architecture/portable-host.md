# Portable windowed host

One host serves Windows, Linux and the browser ([browser-build.md](browser-build.md)): the windowed game runs on SDL3. On
Windows `OpenBeach.exe` (the game linked statically) is the launcher and the host process in one file; the Linux
`obl-sim` ([linux-host.md](linux-host.md)) runs the same game headless, and `-DOBL_LINUX_WINDOWED=ON` builds the Linux
windowed `OpenBeach`. This topic lists each piece of the windowed path and what it still needs from Windows.

## Pieces
"Windows" means what the piece still uses only under `_WIN32`.

| Piece | File(s) | How it works | Windows |
|---|---|---|---|
| Process entry | `launcher/remaster_main.cpp`, `launcher/standalone_host.cpp`, `dll/dllmain.cpp` (`OpenGame`) | SDL `main` (`SDL_main.h`) with UTF-8 options; the host calls `OpenGame` and `platform::RunGame` in process; off Windows `Launch` runs the host in the same process (`chdir` to the game directory) | `CreateProcessW` of the host copy (the test tools find it) |
| Start-up | `platform/platform_main.cpp` (`RunGame`) | static destructors run through `std::atexit`, `platform::Exit` is `std::exit`, an unhandled fault goes to the process filter (`crash.cpp` in tests) | `GetStartupInfoA`, `__ImageBase` |
| `WinMain` | `platform_main.cpp` | one game per game directory through a lock file (`instance_lock.cpp`: temp directory, named by a hash of the game directory; Windows opens it unshared, POSIX `flock`s it) | `HINSTANCE` |
| Main loop | `platform_main.cpp` | `PumpSdlEvents`; quit, close and focus are SDL events | - |
| Quit request | `platform/runtime.cpp` (`RequestQuit`) | `RequestSdlQuit` sets the pump's sticky quit flag | - |
| Window procedure | `sdl_present.cpp` | no subclass, SDL's class procedure is the only one (`window-proc-sdl`) | - |
| Window | `sdl_present.cpp` (`CreateSdlGameWindow`) | the game stores the `SDL_Window*` as its opaque window handle (`SdlGameWindow`, `GetWin32Window` for the Windows-only callers); icon by `SDL_SetWindowIcon` from `remaster/icon.ico` (`core/cursor_file.h` `DecodeIcon`) | `SDL_RegisterApp` class name "Beach Life" (tools find the window by it) |
| Input | `input.cpp` | the image mapping takes the opaque window and SDL's client size (`ClientImageRect`, `GameWindowClientSize`); `POINT`/`SIZE`/`RECT` stay as plain types (`host_linux/win_types`) | - |
| Cursor | `cursor.cpp`, `core/cursor_file.cpp` | SDL colour cursors decoded from the `.cur` files, applied on SDL mouse motion | - |
| Presentation | `sdl_present.cpp`, `renderer_display.cpp` | `PresentBackBuffer(bits, pitch, width, height, rgb555)`; without a GPU renderer SDL's software renderer presents, a failure drops frames | - |
| Renderer life cycle | `renderer_lifecycle.cpp`, `windowed_display.cpp`, `renderer_surface.cpp` | screen rectangle from SDL; every surface record owns a plain heap block | - |
| Window placement | `window_placement.cpp` | `SDL_GetDisplayBounds`/`SDL_GetDisplayUsableBounds`, `SDL_GetWindowBordersSize`, `SDL_GetWindowSizeInPixels` | the resolution list's primary monitor (`MonitorFromPoint`: the list is built before SDL video runs, and headless never starts it) |
| Hidden test window | `hidden_window.cpp` | on Windows `SetWindowPos` (bottom of z-order, no activation), `WS_EX_TOOLWINDOW`, `MessageBoxA`; elsewhere a hidden run is the headless host or SDL's offscreen video driver (`hidden_window_sdl.cpp`) | yes |
| Frame limiter | `frame_limiter.cpp` | `SDL_GetTicksNS` + `SDL_DelayPrecise`; the browser build does not wait ([browser-build.md](browser-build.md)) | - |
| Intro movie | `intro_movie.cpp` | SDL only (the presenter's renderer, `OBL_MOVIE_SKIP_MS` pushes an SDL Escape event; without a renderer the movie is skipped); a state of the main loop on every host | - |
| Audio | `sound_lifecycle.cpp`, `sample_stream.cpp`, `mp3_length_scan.cpp`, the playlist files | SDL audio streams; playlist scanning uses `os::` paths | - |
| System font | `font_construct.cpp` | nothing rasterizes a system font; the debug font (stored by the map view, never drawn) has a blank atlas on every host | - |
| Shell link | `playlist_scan.h` | `.lnk` files in the music folder are not followed on any system | - |
| Host services | `runtime.cpp`, `host_text.cpp`, `config.cpp` | portable with `#ifdef _WIN32` branches, msvcrt formatting on Windows | - |
| Test tools | `platform/test_tools/` | `PrintWindow`, `PostMessage` of keys and `WM_CLOSE`, window class and procedure probes | the window-level probes are Windows-only; the Linux windowed host has its own SDL-event injection |

## How it works
**Events.** The host's main loop calls `PumpSdlEvents()` (`sdl_present.h`): SDL_PumpEvents, then the host's own events are
taken before the input adapter drains its range: `SDL_EVENT_QUIT` and the game window's `SDL_EVENT_WINDOW_CLOSE_REQUESTED`
(the window is hidden, the quit flag set). The quit flag is sticky. `appActive` follows SDL focus events (the event filter).

**Input and cursor.** Mouse buttons, wheel and drag motion are queued as `Input::MouseRecord` and copied into the object
(`Input_InitMouse`'s device is a non-null token, nothing is acquired); time stamps are `SDL_GetTicks`. The cursor shapes are
SDL colour cursors decoded from the `.cur` files (`core/cursor_file.h`: 1/4/8/24/32 bpp DIBs; the monochrome "invert" pixel
becomes black, the game's files have none); `RefreshLocalCursor` applies the arrow over the bars, the shape over the image,
nothing while hidden or during a drag, and never touches the cursor in a hidden run. An `SDL_AddEventWatch` (`input.cpp`)
handles the game window's focus events when SDL queues them (the drag ends and queued input is dropped at once; the drain
still applies them in queue order) and refreshes the cursor on mouse motion. SDL's Windows backend makes focus events only
for the foreground window, so the tests (`test_tools/presenter.cpp` `PushSdlFocus`, `cursor.cpp`, the `input-focus` command)
push SDL focus events.

**Window and presentation.** `CreateSdlGameWindow` returns the `SDL_Window*` that the game keeps as its opaque window handle
(Renderer, Input, error window); `SdlGameWindow(handle)` interprets it and `GetWin32Window(handle)` gives the Win32 handle
only to the Windows-only callers (message boxes, hidden mode, test probes). The window icon test `window-icon-game` checks
`WM_GETICON`. `PresentBackBuffer` takes the 16 bpp buffer, its pitch and format; when no GPU renderer can be created SDL's
software renderer presents (`sdl-software` stat). Placement, the renderer's screen rectangle and the image mapping use SDL's
display bounds and window size in pixels. The intro movie draws only through the presenter's renderer and its skip test
pushes an SDL key event.

**Process services.** The frame limiter waits with `SDL_DelayPrecise` on `SDL_GetTicksNS`. The single-instance check is a
lock file, released when the process ends even after a crash. COM is not initialized.

**One executable.** `openbeach` is an object library linked into `OpenBeach.exe`; the host process calls `OpenGame`
(log, `InitGame`) and `platform::RunGame` (static initializers, `WinMain`) and returns its result. msvcrt stays loaded on
Windows only for the formatting functions `runtime.cpp` resolves (their rounding defines references). The host copy
(`--standalone-host`) stays because the test tools find a game by it; the game's arguments after `--` are only reported (the
game reads no command line). SDL `main` (`SDL_main.h`, UTF-8 arguments), `launch_options` and `standalone_host` work on UTF-8
strings and `std::filesystem` paths (`SDL_GetBasePath` for the executable directory), and `old_saves` spawns the converter
with `posix_spawn` off Windows.

**Linux windowed target.** `OBL_LINUX_WINDOWED` in `src/host_linux/CMakeLists.txt` builds SDL with video, renderer and audio
(`SDL_UNIX_CONSOLE_BUILD` off); the game sources are an object library shared by `obl-sim` and `OpenBeach`;
`dllmain.cpp` is compiled per executable; the windowed modules (`cursor`, `frame_limiter`, `input`, `input_mapping`,
`intro_movie`, `platform_main`, `sdl_present`) build only in `OpenBeach`, with `OBL_WINDOWED_HOST`; `core/key_code.h`
defines the host-independent key-state indices; `hidden_window_sdl.cpp`'s hidden mode selects SDL's offscreen video driver.
