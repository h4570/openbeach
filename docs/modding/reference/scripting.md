# Presentation scripts

A mod may ship Lua scripts that change how the game looks or reacts to input: `scripts/presentation/*.lua`. They run outside
the simulation, in a sandbox, and reach the world only through game commands, so a replay never needs the mod that produced
them. Engine overview: [mods/index.md](../../architecture/mods/index.md). Code: `src/game/mods/script_runtime.cpp` (the runtime),
`src/game/mods/presentation_scripts.cpp` (frames, input and commands of the game).

**API version 1 is experimental** until the official mods are ported: until then a breaking change may happen without a new
version. After that, a breaking change raises the version, and the old one keeps working or the mods screen says why not.

## Files and manifest
```json
{
  "format": "obl.mod", "version": 1, "id": "fps_counter", "title": "FPS counter", "mod_version": "1.0.0",
  "packs": ["base", "l0"],
  "scope": "presentation",
  "api": 1
}
```
- `api`: the script API version the scripts were written for. A mod with scripts must name it; the game refuses a mod that needs
  a newer version than it has, and the message names the mod and both versions.
- `packs`: a mod that does not depend on content may list several packs instead of one `pack` ([mods.md](mods.md#modjson)).
- `scope`: `"world"` (the default) or `"presentation"`, see "Scope" below. Any mod may ship scripts; a world mod is recorded in
  saves and replays anyway.
- The `.lua` files of `scripts/presentation/` (not its subdirectories) load in name order into one Lua state per mod, so they share
  globals. Precompiled chunks are refused.

## Scope
A `"presentation"` mod changes nothing the simulation reads, so saves and replays leave it out. The start-up check refuses a
presentation mod that ships anything but:
- `mod.json`, the files under `scripts/presentation/`, documents at the top (`README.md`, `LICENSE`, `*.txt`);
- text patches `text/<lang>.patch.json` that only add keys: feedback messages keep their formatted text in the world, so a
  changed value of an existing key would make replays depend on the mod. The text loader refuses such a patch.

Catalog records, rules, scenarios, world and object scripts, sprites, sounds and whole files of the pack are for world mods.
Later API blocks (GUI patches, assets built by recipe) add to this list.

## Lifecycle
The runtime starts with the first game frame after the start-up check. Every function is an optional global of the mod.

| Function | When |
|---|---|
| `on_start()` | once, after every mod's files have loaded, in load order |
| `on_frame(dt)` | every game frame after the state's update, in load order; `dt` is the frame's seconds (game tests with `fixed-dt` get that value) |
| `on_input(event)` | for each key or mouse button that went down or up since the last frame, the last mod in load order first |
| `on_stop()` | when the game ends, the last mod first; then the stores are written |

`event` holds `kind` (`"key"` or `"mouse"`) and `phase` (`"down"` or `"up"`); a key event has `key`, the key's name (`"F5"`,
`"LeftShift"`, `"Keypad7"`: the names of `src/core/key_code.h`); a mouse event has `button` (`"left"`, `"right"`) and `x`, `y`
in game pixels. The input API (pointer motion, touch, `consume`) is the next step.

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
| Call | What it does |
|---|---|
| `obl.api` | the API version of the game (a number) |
| `obl.log(...)` | writes its arguments, separated by spaces, to the game log with the mod's id |
| `obl.command(name, fields)` | submits a game command (below) in its text form's name with a table of fields, e.g. `obl.command("set-game-speed", { level = 2 })`; booleans become `0`/`1`. It runs at the start of the next gameplay tick; outside gameplay the game drops it. A command off the list, an unknown or missing field is an error |
| `obl.game.in_world()` | whether a world is in settled gameplay, so a command submitted now runs |
| `obl.store.get(key)` | the value stored under `key`, or `nil` |
| `obl.store.set(key, value)` | stores a boolean, number or string under `key` (1 to 256 bytes); `nil` removes it |

`script_runtime_test` compares this table and the command list below with what the engine registers.

### Store
A small key-value store per mod, `<game dir>/mod-data/<id>.json`, kept outside saves and outside the mod's directory, so
updating the mod keeps it. Keys and string values may take 64 KiB per mod. It is written when the game ends or the mod is
switched off. The browser build keeps no mods yet, so neither does it keep stores.

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
calls of their own with the input and GUI blocks.

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
sandbox escapes (`io`, `os`, `require`, `load`, ...), the command list, the store, and this page's lists. `mod_pack_test`: the
scope check, `packs`, the API version. `tests/l0_mod_script.txt` runs the test mod `tests/mods/l0_script`, whose script sets
the game speed; `tests/replay_l0_mod_script.txt` plays back, without the mod, the replay `l0_mod_script.record.txt` recorded with it.
