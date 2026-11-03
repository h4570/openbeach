# Browser build

The windowed game ([portable-host.md](portable-host.md), the Linux windowed `OpenBeach`) built with Emscripten and run
in a browser tab on SDL3's Emscripten backend (canvas video, Web Audio, DOM input). The headless WebAssembly build under
Node is [web-build.md](web-build.md); the converted pack comes from the browser importer in `importer/web/`. See [the converter](data-formats/converter/index.md) for its layout and API.

## Build
The same CMake entry as the Linux windowed game, configured with `emcmake`:
```
emcmake cmake -S src/host_linux -B build/wasm/browser -G Ninja -DCMAKE_BUILD_TYPE=Release -DOBL_LINUX_WINDOWED=ON
cmake --build build/wasm/browser --target OpenBeach      # bin/OpenBeach.js + .wasm (about 5 MB)
```
`tools/wasm_linux.sh` (CI job `wasm`, `tools/linux_check.ps1` stage `wasm`) builds it as the step "Build browser game"
and checks its pre-js under Node; nothing runs the game in a browser in CI. The build copies the site's page
(`web/index.html`, `web/site.js`) beside it; `tools/web_site.sh` assembles the whole site (see "Site"). Under Emscripten the windowed option selects no X11/Wayland/ALSA switches: SDL's Emscripten
backend has its own video and audio drivers. The target links with `-sENVIRONMENT=web -sFORCE_FILESYSTEM=1` (MEMFS, no
`NODERAWFS`), `--pre-js launcher/browser_files.js`, `-fwasm-exceptions`, `-sALLOW_MEMORY_GROWTH=1` and an 8 MB stack, like `obl-sim`.

## Site
One static directory, our code only (no game data), assembled by `tools/web_site.sh <browser bin> <importer bin> <out>`:
`index.html`, `site.js`, `site_text.js`, `robots.txt` and `sitemap.xml` from `web/`, the logo and the icons from `assets/logo/` (the mark as `favicon.svg`, `favicon.ico` and `apple-touch-icon.png` rendered from it), the screenshots from `assets/screenshots/`, `OpenBeach.js/.wasm` (the pre-js is compiled in), `obl-import-web.js/.wasm`
`import_worker.js`, `language_ini.js` and `shard.js` from the importer build, and `.nojekyll`. `tools/wasm_linux.sh` assembles it in `$build/site`
and the CI job `wasm` uploads it as the artifact `browser-site` after a green run. To try it, unpack it and serve it on
localhost (`python3 -m http.server`): OPFS and Web Locks need a secure context, and the worker does not load from `file:`.

The head carries what a search result and a shared link show: the title, the description, the canonical address (which also covers `?import`), Open Graph tags and a schema.org `WebSite` block, where Google takes the site name from. They stay in English, because Google reads the one URL with an English browser, and `web/site_check.cjs` checks them.

The page comes in the game's six languages (`site_text.js`), picked from `navigator.languages` with English as the fallback; the
switch in the header is remembered in `localStorage`. Right after an import the game takes the page's language if the copy has it.

`web/site.js` is the page's logic. It adds the game script only once OPFS holds `packs/base`, because the pre-js takes
the Web Lock and copies the pack as soon as it loads. Without a pack, or with `?import` in the URL ("Import the game
again" on the ready layer), it shows the drop zone. A folder or a disc image, dropped or picked, goes to `import_worker.js`
(`{source: "files"}` with each file's `webkitRelativePath`, or `paths` for a dropped folder; `{source: "iso"}`). Bars replace
the zone while it runs: the main one counts the whole import (`kStepWeights` in `site.js`, the order of `kStepNames` in
`site_text.js`, which must follow `importer/src/import.cpp`), `done` adds the game script, an error brings the zone back. An import never starts once
the game script is in (the worker replaces the pack the game reads). Long steps report counts (`{step, done, total}`;
the sprites step, from `ImportOptions::count`), which fill the second bar. With enough cores the sprites and maps go to helper workers
([converter](data-formats/converter/index.md), "Sharded conversion"); the bar counts their maps and then their sprites. The page asks for no language before the import, because only the copy knows its tables. The
worker sends `navigator.language` as `preferredLanguage`, and the importer picks the pack's language among the copy's
`Data/text_*.ble` (`ChooseLanguage`): the copy's `BeachLife.ini` language, else the preferred one, else the first table in
the game's order. Only a copy with no table is refused; the CLI, which has no `preferredLanguage`, still refuses a language
the copy lacks. After a successful import the worker reads the result (`obl_import_language`) and sets
`[Language] Language=<index>` in OPFS `BeachLife.ini` (`language_ini.js`, other settings kept), so the INI and `pack.json`
agree. The player chooses on the start layer: its select lists the pack's `text/*.json` and shows the INI's language, stays
visible but disabled for a pack with one table, and a change goes through
`Module.writeSettings` into the game copy and OPFS before the click starts main. The game uses one effective language: when
the requested table is missing, `LanguageSelection::Select` falls back to the pack's `language` (`pack.json`) first, and
`ConstructUntilMovie` stores the result in `Settings::m_language`, so texts and layouts agree. The start layer's "Import saves" passes the file to
`Module.importSaves` and reloads. A line under the drop zone says that adding a copy confirms it is the player's own legal copy, and a started import calls `navigator.storage.persist()` (a refusal is ignored). The importer
checks the known build checksums itself (`source.cpp`; an unknown build is an `error` message). `web/site_check.cjs` runs `site.js` under Node against a fake DOM, OPFS and Worker
for all of this and, with `--site <dir>`, checks that every file the page and the worker load is in the directory.

The page's canvas box takes its aspect from the canvas's pixel size (`fitCanvas` in `site.js` keeps the CSS variable `--aspect`
at width / height through a `MutationObserver`; `index.html` sizes the box by it, letterboxed), so 1280x720 is not forced
into 4:3. The bar's Fullscreen button puts the whole page in fullscreen, not the canvas: SDL takes a fullscreened canvas
for a fullscreen window and sizes it to the screen, so the game would be resampled again. The worker writes
`BeachLife.ini` with `[Beach Life Options] Version=1` (`withLanguage`): the game's loader ignores an INI without it and
would start in English. Progress: `maps` (one item per scenario) and `pack` (one per hashed
file) count like `sprites`; "storing" is the worker writing the pack to OPFS (`persist`, counted by file) and shows as
"Saving to browser storage (n of m)".

## Local probe
`tools/browser_probe.cjs` runs the built site in a headless Edge or Chrome on a developer PC and records what a tab
shows: the game's console log, canvas screenshots at given times, the `AudioContext` state, and scripted left clicks, touch taps and
right drags on the canvas. It serves the site and a converted `packs/base`, copies the pack once into the probe
profile's OPFS (`run/tmp/probe-profile`; about 4 minutes for the full pack), then clicks "Click to start" with a trusted
CDP event. `--game-script` runs a game test script in the tab (as `--script` natively, e.g. `boot-gameplay`), and
memory samples (after the copy and at `memory` steps) go to `memory.json`: wasm memory, MEMFS bytes, V8 heap and its
`ArrayBuffer` backing stores. Usage is in the file's header. `--prepare` runs a page expression before the click; `tools/probe_steps/` holds the language
check (`--prepare language_prepare.js --script language.json`: the start select lists the pack's tables, a change to German
reaches the game's INI and survives a reload) and the resolution check (`resolution.json`, run with `--window-size 1280,720`:
the box keeps each canvas size's aspect). A headless browser reports an 800x600 screen and the game clamps its window to
that "monitor": add `--screen 1920x1080` to anything that depends on the resolution. Headless rendering is slow (SwiftShader), so time steps generously; it is a
diagnosis tool, not a CI check (CI has neither a browser nor the game data). It found the intro movie's stuck end:
the device runs at the `AudioContext` rate (48 kHz), the movie's 44.1 kHz stream kept its last converted frames until a
flush, and the movie waited for the stream to drain (fixed in `intro_movie.cpp`, test `intro_movie_complete.txt`).

## Pieces
How each piece of the windowed host runs in a tab.

| Piece | Where | Native | Browser |
|---|---|---|---|
| Compile and link | `src/host_linux/CMakeLists.txt` | every windowed source builds with `emcc` unchanged | - |
| Main loop | `platform/platform_main.cpp` (`RunWinMain`) | one pass is `RunFrame`; native hosts loop on it, the browser registers `RunBrowserFrame` with `emscripten_set_main_loop(..., 0, true)` (requestAnimationFrame), which ends the game by `FinishWindowedGame` when the quit flag is set | - |
| Frame limiter | `platform/frame_limiter.cpp` | no `SDL_DelayPrecise` in the browser; `RunBrowserFrame` skips an animation frame (events, update and render) while `BrowserFrameDue` says the `MaxFps` schedule is not due yet, with 2 ms slack for the animation frame's jitter, so `MaxFps` at the display rate keeps every frame | - |
| Intro movie | `platform/intro_movie.cpp` | `IntroMovieStart`/`IntroMovieFrame`/`IntroMovieFinish` (`game_host.h`); `game_api::BeginCreate` builds the Game up to the movie, every host runs `RunStartupFrame` (one movie pass) until it ends, then `FinishCreate` builds the rest; the native pass waits 2 ms, the browser's runs once per animation frame | - |
| Process entry | `launcher/remaster_main.cpp`, `standalone_host.cpp`; browser `launcher/browser_main.cpp` | natively SDL `main`, options from `argv`, `chdir` to the game directory, `ConvertStartupSaves` (`posix_spawn` of the converter); the browser build links `browser_main.cpp` instead: shared options, `chdir`, `OpenGame`, `RunGame`, no host process and no old-save conversion (the importer converts saves) | - |
| Game files | every loader (`std::ifstream`, `os::OpenFile`) | the pre-js `launcher/browser_files.js` copies OPFS `packs/base/` (as `import_worker.js` writes it), `BeachLife.ini` and `SaveGames/` into MEMFS `/game` under a run dependency, so `main` starts after the copy with `--game-dir /game` ahead of the page's arguments; no loader becomes asynchronous | - |
| Writes | INI, saves, progress, log | synchronous writes | written to MEMFS; the pre-js compares (mtime, size) of `BeachLife.ini`, `progress.json` and `SaveGames/*` every 2 s and on `visibilitychange`/`pagehide` and writes changes (and deletions) to OPFS; new log text goes to the console |
| Instance lock | `platform/instance_lock.cpp` | `flock` on a temp file | the pre-js takes the Web Lock `openbeach-game` (`ifAvailable`) before copying; a second tab gets `onGameFilesError`; no Web Locks API means no lock. The C++ `flock` stays (harmless in MEMFS) |
| Audio | `sound_lifecycle.cpp`, `sample_stream.cpp` | SDL's Web Audio device; the pre-js holds `main` behind the run dependency `obl-start-click` until the page's "click to start" layer calls `Module.startGame` (shown on `Module.onGameFilesReady`), so `main` runs inside the click and the `AudioContext` starts unsuspended; a click before the wasm finished loading starts `main` later, outside the gesture | - |
| Window size, placement | `window_placement.cpp`, `sdl_present.cpp` | the canvas (`#canvas`, `Module.canvas`) is the window, opaque because the presenter asks for no alpha channel: SDL sets its pixel size, the page's CSS scales it to the largest 4:3 box in the tab (`!important` over SDL's inline size); SDL ignores placement there | - |
| Cursor | `cursor.cpp` | SDL colour cursors from `.cur` files | SDL's Emscripten backend sets CSS cursors from surfaces |
| Quit | `RequestQuit`, `SDL_EVENT_QUIT` | `RunBrowserFrame` cancels the loop, runs `FinishWindowedGame` and calls the page's `Module.onGameEnded`, which hides the canvas and shows "game ended" with a reload button | - |
| Fatal errors, crash report | `app/fatal.cpp`, `ShowErrorAndExit` | log + exit; `dladdr` names | an `abort` shows the message on the page; the JavaScript stack names our functions |
| Threads | - | the game creates none | no `-pthread`, so no `SharedArrayBuffer` and no cross-origin isolation headers on GitHub Pages |
| Page and deploy | `web/`, `tools/web_site.sh` | `web/index.html` with the importer's picker and the game canvas, assembled and uploaded by the `wasm` CI job as the artifact `browser-site` | the CI job `deploy` publishes it ([ci.md](../build/ci.md)) |

## Details
1. **Compile and the main loop.** The browser target builds in the `wasm` CI job; the windowed loop body is one
   function (`RunFrame`) driven by `emscripten_set_main_loop` in the browser and by a `while` loop elsewhere; the frame
   limiter never waits in the browser. Native behavior is unchanged.
2. **Files from browser storage.** A `--pre-js` that, before `main`, copies the OPFS tree the importer wrote
   (`packs/base/...`) and the player's `BeachLife.ini`/`SaveGames` into MEMFS `/game`, then calls `main` with
   `--game-dir /game`. The browser `main` skips the old-save conversion and the launcher's process logic. Check:
   `launcher/browser_files_check.cjs` runs the pre-js under Node against a fake OPFS and MEMFS (copied bytes, other
   packs and stray files left out, an empty `SaveGames/`, no `main` without a pack; the page's `Module.onGameFilesError`
   hears that error); `tools/wasm_linux.sh` runs it after the browser build. Booting the L0 pack in a headless browser
   is not in CI.
3. **Page behavior.** The click-to-start layer (audio unlock), canvas sizing, `MaxFps` pacing by skipping frames, quit to
   a "game ended" page (now `web/index.html`). `browser_files_check.cjs` checks the start gate (no `main`
   before the click; a click during the copy starts it after the copy).
4. **Intro movie as a main-loop state.** `PlayMpeg` split into open/frame/close; the main loop runs movie frames until it
   ends or is skipped, then the game frames. The native hosts use the same state, so the Windows intro tests keep it
   covered.
5. **Writes back to storage.** `browser_files.js` flushes saves, INI and progress to OPFS (`Module.flushGameFiles`;
   a failure calls `Module.onFlushError`), exports them as one JSON file (`Module.exportSaves`/`downloadSaves`), imports
   one into OPFS (`Module.importSaves(text)`, only INI, progress and `SaveGames/<file>`; reload afterwards), prints new
   log text to the console and offers the file (`Module.downloadLog`), and holds a Web Lock. `browser_files_check.cjs`
   covers all of it against a fake OPFS/MEMFS.
   The page has download buttons on the "game ended" layer and "Import saves" on the start layer.
6. **Site and deploy.** The static page joining importer and game ("Site" above), built by the `wasm` job with the pinned
   Emscripten and uploaded as the artifact `browser-site`. A release, or a CI run by hand with `deploy`, publishes it to GitHub Pages
   with `actions/upload-pages-artifact` and `actions/deploy-pages` (`pages: write`, `id-token: write`).
   A run with `deploy` builds the importer with `-O3` (`OBL_IMPORT_O3=1`, otherwise `-O2`): a phone imports faster, and the float flags
   stay the same, so the converted bytes do not move.
   The site lives at `https://openbeach.app`, because OPFS is bound to the origin: moving the address would strand every
   player's import and saves. DNS records for GitHub Pages are DNS only (not proxied, so GitHub can issue the certificate),
   HTTPS is enforced (`.app` requires it in every browser), and an Actions deploy takes the domain from the repository
   settings, so the site needs no `CNAME` file.

## Decisions and limits
- **No Asyncify/JSPI.** Only the main loop and the intro movie loop block; both become per-frame callbacks, so the code
  stays the size and speed of the Node build.
- **Synchronous file system, copied up front.** The game reads its files synchronously; MEMFS filled before `main` keeps
  every loader as it is. Measured with the probe (headless Edge, full `packs/base`, 5598 files, 1032 MB,
  `--game-script` booting the sandbox island): MEMFS keeps file contents in JS `ArrayBuffer`s, **outside** the wasm
  memory, so the copy does not count against wasm32's 2 GB growth limit. The wasm memory is 26 MB after the copy and
  246 MB with the island running (flat over 2 minutes), about 8x below the limit; the tab holds about 1.3 GB in all. So
  the copy stays; WASMFS with an OPFS backend (or a MEMFS that reads a file from OPFS on first open) is the fallback if
  the pack grows past what a tab may hold, not asynchronous loaders. The copy lets MEMFS own each read buffer
  (`canOwn`, no second copy: 80 MB less garbage at the end) and keeps 16 reads in flight; on a busy PC it took
  15-41 s instead of 73-112 s one read at a time. The log line `browser: N game files copied to /game in T ms` gives
  the time in each tab; `browser_files_check.cjs` checks the owned buffers and the overlapping reads.
- **Audio around long frames.** SDL feeds Web Audio from a `ScriptProcessorNode` on the main thread; a frame that builds,
  loads or leaves a world blocks it, and the browser repeated the last buffer (a stuck loop). `game::Update` calls
  `SuspendAudioOutput`/`ResumeAudioOutput` (`game_host.h`, `platform/audio_output.cpp`) around such a frame; in the
  browser they suspend and resume SDL's `AudioContext`, which acts on the audio thread even while the main thread is
  busy. Natively they only count (probe `audio-suspends`, test `audio_long_frame.txt`).
- **Camera drag under the page's pointer lock.** Browsers grant a pointer lock only inside the input event's handler, but
  the game starts a right-drag a frame after the button, so SDL's relative mode got its lock only on a later event and the
  map moved by a pixel. The browser build (`kAbsoluteDrag`, `input_mapping.h`) leaves relative mode off; `site.js` asks for
  the lock in the right button's `pointerdown` and leaves it when the button is up, and `input.cpp` reads SDL's deltas
  while the lock holds. The drag then passes the tab's edge, and the release stays in the page, so no context menu of
  another window opens. Without the lock (refused, or left with Esc) the drag follows the visible pointer
  (`AbsoluteDrag`, unit test `input.absolute_drag_deltas`) and stops at the canvas edge. A right click on a menu locks
  for its short press too: the page cannot know yet whether a drag follows.
- **Touch.** The game only knows a mouse, so in the browser build (`kTouchGestures`) SDL's touch-to-mouse events are off
  (`SDL_HINT_TOUCH_MOUSE_EVENTS`) and `input.cpp` feeds SDL's finger events to `TouchGestures` (`touch_gestures.h`, no SDL,
  unit tests `touch.*`), which turns them into the mouse's records: a tap is a left click, a drag with one finger the camera
  drag (the right button and the motion, scaled from client to game pixels so the map follows the finger), a press and
  hold keeps the left button down until the finger lifts, a tap with two fingers is a right click and a drag with two
  fingers the wheel. The cursor is where the finger is and leaves once it is up and the game has read it, so a tap near the
  edge does not scroll the map. The canvas has `touch-action: none`.
  While the GUI places things (`GuiManager::m_mode` 1 and 5, building and scenery; `CurrentTouchAim`) one finger is the
  cursor instead, because the game places on the press and a tap would place blindly: the finger moves the preview and
  lifting it is the press (`TouchAim::Confirm`); a second finger then turns the preview with the wheel and lifts without
  placing. Painting grass and paving (modes 8 and 7) holds the left button while the finger drags (`TouchAim::Paint`). The
  map moves with the edge scroll there. The keys the game reads cannot be typed, so the page has two buttons below the game
  on touch screens (`pointer: coarse`, `wireTouchKeys` in `site.js`): Space (turns the preview) and Esc (leaves a mode,
  opens the menu), sent to the canvas as keyboard events. Rough by design: the thresholds (`kSlop`, `kTapMs`, `kHoldMs`,
  `kPixelsPerNotch`) are guesses that need a real device, and other keys are not reachable. Native hosts keep SDL's
  touch-to-mouse events.
- **First resolution and sampling.** The GUI has a fixed pixel size, so the page picks the size for the tab: on the first
  start `seedResolution` (`site.js`) writes `[Video]` into the INI, the listed size that fills the tab best at 1.15 screen
  pixels per game pixel or more (`resolutionForTab`); later starts keep the INI's. The canvas keeps the game's pixels and
  the page scales the box with the browser's smooth sampling: nearest sampling at a fractional scale breaks the text.
- **Teardown in the browser** runs `DestroyGame` when the loop ends; static destructors do not run (no `EXIT_RUNTIME`),
  which is harmless since the page is the process.
