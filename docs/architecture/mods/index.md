# The mod system

How mods fit into the engine, for engine work. What a mod author writes is in [../../modding/](../../modding/index.md). **This page
is the overview each modding change updates** (the sub-issues of #123): a new block of the API, a new patch format, a new kind of
script or a change of the scope rule adds its line here.

## Layers and patches
- `mod_pack::Activate` (`src/formats/mod_pack.cpp`) runs once in `InitGame`, after the pack check: it reads `[Mods] Active` (or
  `OBL_MODS`), checks every manifest and stops the game on a refusal. Its result, `mod_pack::Active()`, is the one list the rest
  reads, in load order ([mods.md](../../modding/reference/mods.md#start-up-check)).
- `pack_path` resolves every logical path through the layers: the active mods from last to first, the legacy `mod/`, the pack. A
  file in a mod shadows the whole file below it.
- Some files merge instead of shadowing: text tables (`text/<lang>.patch.json`, `text_load.cpp`), the catalog's record list
  (`catalog/index.patch.json`, `catalog_load.cpp`), JSON data patches (`*.patch.json`, `data_patch.cpp`), and GUI layout
  patches (`gui/<name>.patch.json` with `obl.gui_patch`, `gui_patch.cpp`). Each loader walks `pack_path::Layers` from the
  bottom up, so a later mod wins.
- Building records (`obl.building` v2) accept optional `icon_sprite`, `icon_locked_sprite`, and `preview_sprite` keys so a
  mod building supplies its own build-panel icon and preview without replacing the pack's shared `UI3_bp_thumb` /
  `UI3_bp_preview` atlases (`gui_image_list.h`, `gui_variable_image.cpp`). `tools/make_sprite.py` converts PNG frames into
  indexed `obl.sprite` assets (including building `color`/`depth`/`lightmap` planes and UI icons).

## Two kinds of scripts
| | World and object scripts | Presentation scripts |
|---|---|---|
| Files | `.wsc`, `.lgc` and the object DSL | `scripts/presentation/*.lua` |
| Runs | inside the simulation's tick | after the state's update, outside the tick (`mods::PresentationFrame`, `src/game/game/game.cpp`) |
| Reaches the world | directly; part of the behavior contract | only through game commands ([commands.md](../commands.md)) |
| Saves and replays | the mod is recorded | the mod is not recorded |
| Code | `src/game/world/`, `src/game/objects/` | `src/game/mods/script_runtime.cpp` (one sandboxed Lua 5.4 state per mod), `presentation_scripts.cpp` (the game's host) |

The presentation runtime is pure logic over a `ScriptHost` (log, submit a command, "in a world?"), so its unit tests run it with
a fake host. The interpreter comes from `cmake/lua.cmake`, linked into `obl_common` on Windows and into every target of
`src/host_linux` (Linux, WebAssembly, Android) and `tests/unit/linux`. The API, the sandbox and the command list:
[scripting.md](../../modding/reference/scripting.md).

## The scope rule
`mod.json` names a scope: `"world"` (the default) or `"presentation"`.
- A world mod may change anything a pack holds; saves and replays record it with its content hash, and a replay with other world
  mods is refused.
- A presentation mod reaches the simulation only through the commands open to scripts, which the replay records, so a replay
  never needs it (`mod_pack::Recorded`, used by `ActiveReplayMods` and the save writer). For that to hold, the start-up check
  allows it only a list of files: `mod.json`, `scripts/presentation/`, documents at the top, `gui/<name>.patch.json` (a GUI
  layout patch, or a JSON data patch of a presentation file), and text patches that only add keys
  (feedback messages keep their formatted text in the world hash, so a changed text would reach it). Sprites, sounds and other pack
  files are not on the list, since a frame count or the size of a sound bank can steer the simulation.
- Every later block that lets presentation mods ship a new kind of file (GUI patches, assets built by recipe) extends that list in
  `mod_pack.cpp` (`PresentationMayShip`) and says why the file cannot reach the simulation.

## The input path
`PresentationFrame` turns the `ui::Input` state of each frame into down and up events for keys and mouse buttons and hands them to
`on_input`, the last mod in load order first. The events do not yet go around the game's own input: a script sees what the game
sees. Pointer motion, touch, `consume` (a mod keeps an event from earlier mods and the game) and the camera are the next block.

## Saves and replays
- With no active mods nothing changes: saves have no `mods` key, replays no `mods` line, and the world hashes and goldens stay.
- Only world mods are listed ([saves.md](../data-formats/saves.md#mods), [commands.md](../commands.md#replay-file)).
- A presentation mod's commands are recorded like the GUI's. Replaying a file while a mod that sends commands is active fails:
  during a replay the recording owns the input, and the script's commands are counted as ignored.
- `tests/replays/l0_mod_script.replay` is recorded with the presentation test mod `l0_script` and replayed without it
  (`tests/replay_l0_mod_script.txt`).

## Settings and the store
No setting in `openbeach.ini` changes the simulation. The per-mod store of the presentation scripts (`<game dir>/mod-data/<id>.json`)
lives outside saves and is never read by the simulation.
