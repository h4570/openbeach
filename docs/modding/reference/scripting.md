# Presentation scripts

A mod may ship Lua scripts that change how the game looks or reacts to input: `scripts/presentation/*.lua`. They run outside
the simulation, in a sandbox, and reach the world only through game commands, so a replay never needs the mod that produced
them. Engine overview: [mods/index.md](../../architecture/mods/index.md). Code: `src/game/mods/script_runtime.cpp` (the runtime),
`src/game/mods/presentation_scripts.cpp` (frames, input, commands and the overlay of the game).

**API version 1 is experimental** until the official mods are ported: until then a breaking change may happen without a new
version. After that, a breaking change raises the version, and the old one keeps working or the mods screen says why not.

## Files and manifest
```json
{
  "format": "obl.mod", "version": 1, "id": "fps_counter", "title": "FPS counter", "mod_version": "1.0.0",
  "packs": ["base", "l0"],
  "scope": "presentation",
  "api": 1,
  "takes_over": ["input.touch"]
}
```
- `api`: the script API version the scripts were written for. A mod with scripts must name it; the game refuses a mod that needs
  a newer version than it has, and the message names the mod and both versions.
- `packs`: a mod that does not depend on content may list several packs instead of one `pack` ([mods.md](mods.md#modjson)).
- `scope`: `"world"` (the default) or `"presentation"`, see "Scope" below. Any mod may ship scripts; a world mod is recorded in
  saves and replays anyway.
- `takes_over`: default behaviors the mod's scripts replace, see "Taking over a default" below. Only a mod with scripts may name
  them.
- The `.lua` files of `scripts/presentation/` (not its subdirectories) load in name order into one Lua state per mod, so they share
  globals. Precompiled chunks are refused.

## Scope
A `"presentation"` mod changes nothing the simulation reads, so saves and replays leave it out. The start-up check refuses a
presentation mod that ships anything but:
- `mod.json` (with `settings`, [mods.md](mods.md#settings), which a world mod may not declare), the files under `scripts/presentation/`, documents at the top (`README.md`, `LICENSE`, `*.txt`);
- `overlay/<name>.png`: images for `obl.overlay.image`. No pack has an `overlay/` directory, so they shadow nothing;
- text patches `text/<lang>.patch.json` that only add keys: feedback messages keep their formatted text in the world, so a
  changed value of an existing key would make replays depend on the mod. The text loader refuses such a patch;
- `gui/<name>.patch.json`: a GUI layout patch ([gui-patches.md](gui-patches.md)), or a JSON data patch of a presentation file
  ([data-patches.md](data-patches.md), such as `gui/object_browser.patch.json`).

Catalog records, rules, scenarios, world and object scripts, sprites, sounds and whole files of the pack are for world mods.
Later API blocks (assets built by recipe) add to this list.

## Lifecycle
The runtime starts with the first game frame after the start-up check. Every function is an optional global of the mod.

| Function | When |
|---|---|
| `on_start()` | once, after every mod's files have loaded, in load order |
| `on_input(event)` | for each input event, right after the input update and before the game reads it; the last mod in load order first, until one consumes it (below) |
| `on_input_observe(event)` | for every input event after `on_input`, consumed or not, the last mod first; it cannot consume |
| `on_frame(dt)` | every game frame after the state's update, in load order; `dt` is the frame's seconds (game tests with `fixed-dt` get that value) |
| `on_setting(id, value)` | after the player changed a setting the mod declares ([Settings](#settings)): once per changed setting, in load order, with the new value |
| `on_action(event)` | when the player clicks a button of a GUI patch whose action is `Mod:<this mod's id>.<event>` ([gui-patches.md](gui-patches.md)), or presses a key of one of the mod's own actions, with the action's id ([actions.md](actions.md)) |
| `on_stop()` | when the game ends, the last mod first; then the stores are written |

## Input events
`event` holds `kind` and `phase`:

| `kind` | `phase` | Other fields |
|---|---|---|
| `"key"` | `"down"`, `"up"` | `key`: the key's name (`"F5"`, `"LeftShift"`, `"Keypad7"`: the names of `src/core/key_code.h`) |
| `"mouse"` | `"down"`, `"up"` | `button` (`"left"`, `"right"`), `x`, `y` |
| `"pointer"` | `"move"` | `x`, `y`: the mouse moved the game's pointer (once per frame) |
| `"wheel"` | `"move"` | `delta` in notches (positive away from the player), `x`, `y` |
| `"touch"` | `"down"`, `"move"`, `"up"`, `"cancel"` | `finger` tells the fingers apart, `x`, `y`; `cancel` (focus lost) ends every finger |

Positions are game pixels; `-300, -300` is off the game's image. Touch events come where the game reads fingers itself: the
browser and Android. On a desktop the system turns a touch screen into mouse events.

- **Consume:** `event:consume()` in `on_input` stops the event: earlier mods' `on_input` and the game do not see it. A consumed
  key or button press stays hidden from the game until it goes up; a consumed wheel event's notches are taken back; a consumed
  finger `down` keeps that finger from the game's touch gestures (a tap is a click, a drag moves the camera) until it is up.
  Consuming an `up` or a `move` changes nothing for the game.
- **Order:** two mods consuming the same kind of event cooperate: the later in load order gets first pick. A mod that took over
  the event's area gets the event first.
- **Observe:** `on_input_observe(event)` sees every event with `consumed_by` (the id of the mod that consumed it, or `nil`), for a
  frame counter or "the last input was touch". Calling `consume` there is an error.
- Inputs scripts make up (`obl.input`) reach the game, not the scripts.

## Taking over a default
`takes_over` in `mod.json` names default behaviors the mod replaces. While the mod runs, the game skips that default and the mod
does its work with the calls below.

| Name | The default it replaces |
|---|---|
| `input.touch` | the touch gestures: a tap is a left click, a held finger a held button, a drag moves the camera, two fingers scroll or right-click. The mod gets every touch event first and turns fingers into `obl.input` clicks and `obl.camera` moves |
| `input.mouse_drag` | the camera drag with the right button held (the right click itself still reaches the game); the mod gets mouse and pointer events first |
| `camera.edge_scroll` | the camera scrolling while the cursor touches the screen edge |
| `action.<name>` | an engine action such as `action.game.pause`: the engine ignores its keys ([actions.md](actions.md)) |

- **Safety net:** when the mod fails, runs out of its budget or stops, the default comes back at once, so a broken mod cannot
  leave a phone without touch. The log says `presentation scripts: "input.touch" is the engine's again (mod "<id>" stopped)`.
- **Conflicts:** two mods taking over one default: the later in load order wins; the start-up log names both
  (`mod takes_over: "input.touch" is taken over by "a", "b"; "b" wins (later in load order)`). If the winner stops, the other
  takes over.
## Errors
A script error, an error while loading a file, a used-up budget or the memory cap switches off that mod: the game logs
`presentation scripts: mod "<id>" is switched off: <file>:<line>: <message>` and keeps running with the other mods. The mod's
`on_stop` is not called; its store is still written.

## Sandbox
- Libraries: `base`, `coroutine`, `math`, `string`, `table`, `utf8`. The interpreter is built without `io`, `os`, `package`
  (`require`) and `debug`, and the sandbox removes `load`, `loadfile`, `dofile`, `collectgarbage`, `string.dump` and
  `math.randomseed`. `math.random` starts from a fixed seed. `print` writes to the game log with the mod's id.
- No file, network, process or clock access beyond the API below.
- **Instruction budget:** each call into a mod (a lifecycle function, or running one file at load) may run 10 million Lua VM
  instructions. Once they are used up every further instruction fails, also one after a `pcall` that caught the error.
- **Memory cap:** 32 MiB per mod.
- A mod logs at most 200 lines per run.

## API
Coordinates are game pixels (numbers, rounded down). Calls that need a world answer `nil` or `false` outside one. A wrong argument is
an error of the script, which switches off the mod like any other error.

| Call | What it does |
|---|---|
| `obl.api` | the API version of the game (a number) |
| `obl.platform` | `"windows"`, `"linux"`, `"macos"`, `"web"` or `"android"` |
| `obl.log(...)` | writes its arguments, separated by spaces, to the game log with the mod's id |
| `obl.command(name, fields)` | submits a game command (below) in its text form's name with a table of fields, e.g. `obl.command("set-game-speed", { level = 2 })`; booleans become `0`/`1`. It runs at the start of the next gameplay tick; outside gameplay the game drops it. A command off the list, an unknown or missing field is an error |
| `obl.settings.get(id)` | the player's value of a setting the mod declares in `mod.json`, or its default: a boolean, a string or a number ([Settings](#settings)); another id is an error |
| `obl.store.get(key)` | the value stored under `key`, or `nil` |
| `obl.store.set(key, value)` | stores a boolean, number or string under `key` (1 to 256 bytes); `nil` removes it |

### Input
| Call | What it does |
|---|---|
| `obl.input.click(x, y, button)` | a click at `(x, y)`: the game sees the press in this update and the release in the next. `button` is `"left"` (the default) or `"right"` |
| `obl.input.press(x, y, button)` | the button goes down at `(x, y)` and stays down (a long press, a drag) |
| `obl.input.release(x, y, button)` | the button goes up at `(x, y)` |
| `obl.input.move(x, y)` | the game's pointer is at `(x, y)` until the mouse moves or `obl.input.move()` gives it back |
| `obl.input.last_pointer()` | `"mouse"` or `"touch"`: what moved the pointer last; `nil` before any |

A made-up input called in `on_input` reaches the game in the same update; one called in `on_frame` in the next. A click or press
also holds the pointer where it happened, so the game reads the release there.

### Camera
| Call | What it does |
|---|---|
| `obl.camera.position()` | the camera's top-left corner in map pixels, `x, y`; `nil` outside a world |
| `obl.camera.set_position(x, y)` | moves the camera there, clamped to the map as the game clamps it; `false` outside a world |
| `obl.camera.pan(dx, dy)` | moves the camera by `(dx, dy)` map pixels |
| `obl.camera.zoom()` | the zoom as a scale: `2` (in), `1`, `0.5` (out) |
| `obl.camera.set_zoom(scale)` | the nearest of the game's three zoom levels; it applies in the next frame, keeping the view's centre |

### GUI
GUI state that is not a game command: nothing here reaches the simulation or a replay.

| Call | What it does |
|---|---|
| `obl.gui.show(id)` | shows the control with that id ([gui-patches.md](gui-patches.md)); `false` when there is none |
| `obl.gui.hide(id)` | hides it |
| `obl.gui.move(id, x, y)` | moves it to `(x, y)` in its dialog |
| `obl.gui.open(dialog)` | opens the dialog (a panel) by name |
| `obl.gui.close(dialog)` | closes it |
| `obl.gui.move_dialog(dialog, x, y)` | moves the dialog |
| `obl.gui.select(id)` | selects the object with that id as the player's click does, opening its info dialog |
| `obl.gui.place(kind, key)` | the build tool for the catalog key, `kind` `"building"` or `"scenery"`: the preview follows the cursor, and the player's click places it (a game command) |
| `obl.gui.cancel()` | leaves the build tool |

### Overlay
Drawn over the finished frame, after the GUI and the cursor, and never read by the game. Call these in `on_frame`: the overlay of
a frame is what that frame's scripts drew. Colors are `0xAARRGGBB`; an alpha below `0xFF` blends. A mod may make 4096 drawing calls
per frame.

| Call | What it does |
|---|---|
| `obl.overlay.rect(x, y, w, h, color)` | a filled rectangle |
| `obl.overlay.frame(x, y, w, h, color)` | the outline of a rectangle, one pixel wide |
| `obl.overlay.text(x, y, text, font, color)` | UTF-8 text in a font of the pack (`guifont8`, `guifont8b`: the names under the pack's `fonts/`); `color` defaults to white; a font the pack lacks is an error |
| `obl.overlay.measure(text, font)` | the text's width and height in pixels |
| `obl.overlay.image(x, y, name, w, h)` | a PNG (8-bit RGB or RGBA, at most 1024 pixels a side) from the mod's `overlay/`, at its size or scaled to `w` x `h` |
| `obl.overlay.view(sx, sy, sw, sh, x, y, w, h)` | the frame's region `(sx, sy, sw, sh)`, as it was before the overlay, scaled into `(x, y, w, h)`: a magnifier |

The overlay is off in test runs (golden images do not depend on mods) unless the test says `mod-overlay on`.

### Screen and frame
| Call | What it does |
|---|---|
| `obl.screen.size()` | the game's width and height in pixels |
| `obl.screen.aspect()` | width divided by height |
| `obl.screen.safe_area()` | `{left, top, right, bottom}`: how far a phone's notch, rounded corners or system bars reach into the game's image, in game pixels (0 on a desktop) |
| `obl.stats.frame_ms()` | the last frame's time in milliseconds |
| `obl.stats.fps()` | frames per second over the last 30 frames |

### Game state
A copy taken after the simulation's tick: reading it cannot change the simulation.

| Call | What it does |
|---|---|
| `obl.game.in_world()` | whether a world is in settled gameplay, so a command submitted now runs |
| `obl.game.state()` | `{money, day, hour, speed, paused, weather, guests, mood}`, or `nil` outside a world. `speed` is the level 0 to 2; `weather` one of `"heat_wave"`, `"normal"`, `"overcast"`, `"light_rain"`, `"heavy_rain"`, `"thunderstorm"`; `mood` the guests' mean happiness |
| `obl.game.buildings()` | a list of `{id, kind, key, x, y, state}`: the buildings with their catalog key, tile and state number |
| `obl.game.selected()` | the selected object as `{id, kind, key, x, y, state}` (`kind`: `"building"`, `"guest"`, `"staff"`, `"scenery"`, `"vehicle"`, `"other"`), or `nil` |
| `obl.game.hovered()` | the object under the cursor, the same way |

`script_runtime_test` compares the tables of this section and the command list below with what the engine registers.

### Store
A small key-value store per mod, `<game dir>/mod-data/<id>.json`, kept outside saves and outside the mod's directory, so
updating the mod keeps it. Keys and string values may take 64 KiB per mod. It is written when the game ends or the mod is
switched off. The browser build keeps no mods yet, so neither does it keep stores.

### Settings
`settings` in `mod.json` ([mods.md](mods.md#settings)) declares toggles, choices and numbers. `obl.settings.get("wide")` gives
a boolean for a toggle, the option's `value` (a string) for a choice, and a number for a number setting (a Lua integer when the
setting's bounds, step and default are whole, so `math.type` is `"integer"`). The values are read when the scripts start, so
`on_start` already sees them. A script only reads them: the player changes them on the mods screen, and `on_setting` follows,
with the new value, for every setting whose value really changed. A script that wants to remember something of its own uses the
store. Settings sit in `openbeach.ini`, never in a save, and the simulation never reads them.

## Commands
The commands a script may send are the GUI's ([commands.md](../../architecture/commands.md#command-set-format-version-1)), with
the same fields and executor checks. Replays record them like any other command.

| Command | Fields |
|---|---|
| `set-paused` | `paused` |
| `resume-game` | |
| `set-game-speed` | `level` |
| `place-building` | `key x y dir` |
| `place-scenery` | `key x y dir` |
| `paint-surface` | `key x y` |

A replay records a script's commands like the GUI's, so it plays back without the mod; playing a replay while a mod that sends
commands is active fails, since during a replay the recording owns the input and the script's commands count as ignored.

Selecting an object, opening a panel and the build tool's preview are not commands: they change no world state, and come as
calls of their own (`obl.gui`).

## Interpreter
Lua 5.4.9 (MIT), fetched by CMake from lua.org with a pinned SHA-256 (`cmake/lua.cmake`, like zlib) and compiled as C++, so a
script error unwinds the engine's frames as an exception, also in the WebAssembly build. Why Lua 5.4 and not Luau:
- Lua is about 30 C files that build with every compiler the game uses (MSVC, GCC, Clang, Emscripten, the Android NDK) without
  a build system of its own; Luau is a larger C++ project with its own CMake tree and compiler, analysis and VM libraries.
- What the sandbox needs is in the stock API: a custom allocator for the memory cap, a count hook for the budget, and leaving
  libraries out at build time. Luau's built-in sandbox and interrupts would save little code.
- The 5.4 line is stable and widely known by modders; 5.4.9 is its latest bug-fix release. Lua 5.5 changes the language
  (global declarations) and is newer than the official mods need.

## Tests
`script_runtime_test` (unit): load order, an error switches off only its mod, the budget stops endless loops, the memory cap,
sandbox escapes (`io`, `os`, `require`, `load`, ...), the command list, the store, the settings (typed values, a bad value, `on_setting`, the `[Mod.<id>]` section), consume and observe, taking over and the safety
net, GUI actions, the calls of each block against a fake host, and this page's lists. `input_events_test`: events from the input
update, consumed keys and buttons hidden from the game, made-up clicks, the script's pointer, finger events. `mod_pack_test`: the
scope check, `packs`, the API version, `takes_over`. Game tests on L0 with the test mods of `tests/mods/`:
- `tests/l0_mod_script.txt` (`l0_script`): the script sets the game speed; `tests/replay_l0_mod_script.txt` plays back, without
  the mod, the replay `l0_mod_script.record.txt` recorded with it;
- `tests/l0_mod_settings.txt` (`l0_settings`): settings and `on_setting`;
- `tests/l0_mod_script_touch.txt` (`l0_touch`): taking over `input.touch` turns a tap into a right click; the mod fails on purpose
  and a tap is a left click again;
- `tests/l0_mod_script_consume.txt` (`l0_consume`): consumed clicks place nothing;
- `tests/l0_mod_script_camera.txt` (`l0_camera`): a script pans and zooms the camera;
- `tests/l0_mod_script_overlay.txt` (`l0_overlay`): the overlay's pixels, and an `expect-shot`;
- `tests/l0_mod_script_state.txt` (`l0_state`): a mod reading the game state every frame keeps the world hash of the run
  without it.
