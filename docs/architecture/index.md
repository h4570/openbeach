# Architecture

How the game is put together: the boundaries between simulation, GUI and hosts (game window, headless `obl-sim`) and
the contracts between them.

| Topic | Summary |
|---|---|
| [bots/](bots/index.md) | Scenario bots, in parts: the contract (a read-only world view and queries in, commands out), win and loss from the campaign scripts, `.bot` run files on the headless host; metrics, tolerance bands over the recorded seeds (`tests/bots/<run>.bands.json`), `tools/bot_runs.py` and its evidence, the diagnostics; the catalog of bots so far; scenario 12 in depth; how to write the next bot. |
| [data-formats/](data-formats/index.md) | The data formats: packs read by the game and produced by `obl-convert` from the user's copy, the provenance rule, data tiers L0/L1/L2, and the formats for sprites, audio, texts, buildings, maps, scenarios and saves. |
| [commands.md](commands.md) | The command layer: player input as semantic commands applied at a tick, the text form, the replay file with world-hash checkpoints, and how recorder, replay and headless `obl-sim` share it. |
| [headless/](headless/index.md) | Headless invocation, test-script and batch contracts, replay and save lifecycle, and platform switches and import inventory. The Windows and Linux hosts share the simulation without window, audio or input devices. |
| [input.md](input.md) | The Input object shared by the Windows host and the Linux `obl-sim`: pure state in `src/game/ui/input_state.h`, the `Input_*` functions in `input_ops.cpp`, and the device backends (SDL on Windows, none headless). |
| [map.md](map.md) | The map module (`src/game/map/`): the grid, its cells, areas and boat route, the route search and routes, the path tiles, the sea animation, their lifetimes and tests. |
| [game.md](game.md) | `Game`, its `Settings` and the game state machine: the `Game` record (`src/game/game/game.h`), `game_states.h` and what the Game owns. |
| [presentation.md](presentation.md) | Presentation: sprites, map view, renderer, GUI, fonts, cursor and audio, their records and boundaries; golden images and map hashes as the tests. |
| [platform.md](platform.md) | The `src/platform/` layer: lint rules (Windows/SDL allowed), build placement, its files (input, SDL presentation, intro movie, sound lifecycle, playlist scan/ID3) and the interfaces the game declares for it. |
| [presentation-render.md](presentation-render.md) | UI image loading, text, blit and line runtime boundaries, palette modifiers, save panels, the renderer facade and portable algorithms. |
| [map-view.md](map-view.md) | Camera, terrain projection, background streaming and rebuilding, object culling and island image caching; behavior contracts and tests. |
| [presentation-fonts-audio.md](presentation-fonts-audio.md) | Font types and text rendering, scripted and loop sound, the `Sound` records and APIs. |
| [objects.md](objects.md) | Object manager ownership, ids and queries, moving and static classes, shared routes and records, and persistence checks. |
| [linux-host.md](linux-host.md) | The Linux headless `obl-sim`: building and running in WSL, the test runner, process lifecycle, RNG, host services and game paths. |
| [portable-host.md](portable-host.md) | The windowed game on SDL3 on Windows, Linux and the browser: each piece of the windowed path (entry, start-up, window, input, cursor, presentation, frame limiter, intro movie, audio, launcher) and what it still needs from Windows. |
| [browser-build.md](browser-build.md) | The windowed host on SDL3's Emscripten backend: the build, the site, the local probe, and each browser piece (main loop, waits, files from OPFS, audio unlock, page, deploy). |
| [web-build.md](web-build.md) | The unit tests and the headless `obl-sim` built with Emscripten and run under Node (all synthetic unit tests pass, L0 world hashes equal x64), the flags and shims, threads, and what the 32-bit build covers. |

- [Sprites](sprites.md): animation caches, resource ownership, instance rendering and baking.
