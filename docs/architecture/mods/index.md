# The mod system

How mods fit into the engine, for engine work. What a mod author writes is in [../../modding/](../../modding/index.md). **This page
is the overview each modding change updates** (the sub-issues of #123): a new block of the API, a new patch format, a new kind of
script or a change of the scope rule adds its line here.

## Layers and patches
- `mod_pack::Activate` (`src/formats/mod_pack.cpp`) runs once in `InitGame`, after the pack check: it reads `[Mods] Active` (or
<<<<<<< HEAD
  `OBL_MODS`), checks every manifest (including `conflicts` incompatibilities) and stops the game on a refusal. Its result, `mod_pack::Active()`, is the one list the rest
  reads, in load order ([mods.md](../../modding/reference/mods.md#start-up-check)). `mod_pack::ActivateList` applies a new list at runtime when the mods screen changes presentation mods.
- `pack_path` resolves every logical path through the layers: the active mods from last to first, the legacy `mod/`, the pack. A
  file in a mod shadows the whole file below it.
=======
  `OBL_MODS`), checks every manifest and stops the game on a refusal. Its result, `mod_pack::Active()`, is the one list the rest
  reads, in load order ([mods.md](../../modding/reference/mods.md#start-up-check)).
- `pack_path` resolves every logical path through the layers: the active mods from last to first (each mod's `mods/<id>/`
  followed by its recipe-derived cache `mod-cache/<id>/`), the legacy `mod/`, the pack. A file in a mod shadows the whole file
  below it.
- Asset recipes (`recipes/*.json`, `obl.asset_recipe`, [recipes.md](../../modding/reference/recipes.md)): `obl-convert recipes`
  (and `obl-convert import` / the browser worker `obl_build_mod_recipes`) run deterministic integer/fixed-point image operations
  (`crop`, `mirror`, `scale`, `blur`, `pad`) over pack PNGs and cache PNG or indexed `obl.sprite` outputs in `mod-cache/<id>/`
  keyed by recipe and source SHA-256 (`importer/src/writers/asset_recipe.cpp`). Keeping outputs outside `mods/<id>/` leaves
  `ModContentHash` unchanged across packs. Missing sources disable only the outputs that need them.
>>>>>>> 2897537 (docs: document mod asset recipes and converter integration)
- Some files merge instead of shadowing: text tables (`text/<lang>.patch.json`, `text_load.cpp`), the catalog's record list
  (`catalog/index.patch.json`, `catalog_load.cpp`), the object script vocabulary (`scripts/object_vocabulary.patch.json`,
  `object_vocabulary.cpp`), JSON data patches (`*.patch.json`, `data_patch.cpp`), and GUI layout
  patches (`gui/<name>.patch.json` with `obl.gui_patch`, `gui_patch.cpp`). Each loader walks `pack_path::Layers` from the
  bottom up, so a later mod wins (or for vocabulary names, ids are assigned after the pack's in load order and collisions are refused).
- Object scripts (`scripts/object/*.obs`) can be attached per guest type (`"script"` in `obl.guest_set`) or per staff record
  (`"object_script"` in `obl.staff`, keeping `"script"` for the profession class). `LoadScriptProgramVocabulary` merges active
  mods' vocabulary patches and validates every mod `.obs` in strict mode at start-up (naming mod, file, and line on error);
  `WorldScriptHost::FindOrLoad` loads custom object scripts on demand, and `LoadSaveOrigin` uses the same merged vocabulary so
  saves write and read mod state names ([object-scripts.md](../../modding/reference/object-scripts.md)).
- Building records (`obl.building` v2) accept optional `icon_sprite`, `icon_locked_sprite`, and `preview_sprite` keys so a
  mod building supplies its own build-panel icon and preview without replacing the pack's shared `UI3_bp_thumb` /
  `UI3_bp_preview` atlases (`gui_image_list.h`, `gui_variable_image.cpp`). `tools/make_sprite.py` converts PNG frames into
  indexed `obl.sprite` assets (including building `color`/`depth`/`lightmap` planes and UI icons).

## Official mod sources

Official mods live next to the L0 pack under `data/mods/<id>/`. The Windows test runner and the Linux CI
overlay tests copy these sources into the test game's `mods/` directory. Each test opts in through
`OBL_MODS`, using the regular manifest, pack and scope checks. Release staging copies the L0 pack and
leaves the mod sources in the repository. The [official examples](../../modding/official-mods.md) link to
each mod and its L0 test.

## Two kinds of scripts
| | World and object scripts | Presentation scripts |
|---|---|---|
| Files | `.wsc`, `.lgc`, `scripts/world/*.wsc` and the object DSL | `scripts/presentation/*.lua` |
| Runs | inside the simulation's tick; mod world scripts after the scenario's script in load order | outside the tick: input after the input update (`mods::PresentationInput`), the frame after the state's update (`mods::PresentationFrame`), the overlay before the present (`src/game/game/game.cpp`) |
| Reaches the world | directly; part of the behavior contract | only through game commands ([commands.md](../commands.md)) |
| Saves and replays | the mod is recorded | the mod is not recorded |
| Code | `src/game/world/`, `src/game/objects/` | `src/game/mods/script_runtime.cpp` (one sandboxed Lua 5.4 state per mod), `presentation_scripts.cpp` (the game's host) |

**Mod world scripts** (`scripts/world/*.wsc` of a world mod, [world-scripts.md](../../modding/reference/world-scripts.md)): the
script host (`world_script_host.h`) owns them beside the scenario's script as `ModWorldScript` records (a parsed `WorldScript`, its
own flags and 32 variables); `ForEachWorldScript` gives the order of one tick (scenario first, then load order). While a mod
script runs, `world_script.cpp` sends `Flag<n>`, `Var<n>` and `ChangeState` to that record (`ChangeState` carries the load-order
index + 1 in the message's `flags`, `world_receive_logic.h`). The parser adds `OnEvent <Event>` lists; four events
(`world_script_api.cpp`) are dispatched from the day cycle, construction, guest arrival and the weather schedule, and only to mod
scripts. `world::CheckModWorldScripts` refuses scripts with winning or losing calls at start-up. The save writes
`world.mod_scripts` and the world hash a `ModWorldScripts` class only while a mod script is active, so worlds without one keep
their saves and hashes.

The presentation runtime is pure logic over a `ScriptHost` (log, submit a command, "in a world?", settings, and the input, camera,
GUI, overlay and game-state calls, each with a do-nothing default), so its unit tests run it with a fake host. The game's host
(`presentation_scripts.cpp`) reaches the GUI through `gui_patch_runtime`, the camera through `MapView`, reads the game state into a
`GameSnapshot` once per update (`game_snapshot.cpp`) and draws the overlay into the 16-bit back buffer (`overlay_draw.cpp`). The interpreter comes from `cmake/lua.cmake`, linked into `obl_common` on Windows and into every target of
`src/host_linux` (Linux, WebAssembly, Android) and `tests/unit/linux`. The API, the sandbox and the command list:
[scripting.md](../../modding/reference/scripting.md).

## The scope rule
`mod.json` names a scope: `"world"` (the default) or `"presentation"`.
- A world mod may change anything a pack holds; saves and replays record it with its content hash, and a replay with other world
  mods is refused.
- A presentation mod reaches the simulation only through the commands open to scripts, which the replay records, so a replay
  never needs it (`mod_pack::Recorded`, used by `ActiveReplayMods` and the save writer). For that to hold, the start-up check
  allows it only a list of files: `mod.json`, `scripts/presentation/`, documents at the top, `overlay/<name>.png` (images only
  the overlay draws), `recipes/<name>.json` whose `output` is `overlay/<name>.png` or `images/screens/<name>.png` (screen and
  overlay pictures never reach the simulation), `gui/<name>.patch.json` (a GUI
  layout patch, or a JSON data patch of a presentation file), and text patches that only add keys
  (feedback messages keep their formatted text in the world hash, so a changed text would reach it). Sprites, sounds and other pack
  files are not on the list, since a frame count or the size of a sound bank can steer the simulation.
- `settings` in `mod.json` belong to presentation mods only: the start-up check refuses a world mod that declares any, since
  nothing in a world mod reads them and world scripts must never see one.
- Every later block that lets presentation mods ship a new kind of file (GUI patches, assets built by recipe) extends that list in
  `mod_pack.cpp` (`PresentationMayShip`) and says why the file cannot reach the simulation.

## The input path
- `PresentationInput` runs between the input update and everything that reads `ui::Input` (the command tick, the state, the
  GUI). `InputEvents` (`input_events.cpp`, pure, unit-tested) turns the update into events: keys, mouse buttons, one pointer move,
  one wheel event. Each goes to `on_input` until a mod consumes it, then to every `on_input_observe`. A consumed event is taken back
  out of the input object (a press stays hidden until its release), so the game never sees it, on every platform.
- Inputs a script makes up (`obl.input.click`, ...) are queued and written into the input object after the next input update, so
  the game reads "pressed in this update" as it does for the mouse.
- Fingers do not pass through the input object: the platform hands each finger event to `mods::RouteTouch`
  (`input_routing.h`) before its touch gestures. A consumed down hides that finger from the gestures; while a mod owns
  `input.touch` they see no finger at all.
- Taking over: `input_routing` holds which default areas a running mod owns. `world_view.cpp` skips the edge scroll
  (`camera.edge_scroll`) and the right-button camera drag (`input.mouse_drag`) while they are owned. The owner follows the
  running mods after every call into the scripts, so a failing owner gives its area back in the same update. With no active
  mod nothing is owned and no event is built.
- The overlay is a list of drawing calls made in `on_frame`, drawn after the GUI and the cursor; test runs hide it unless a test
  asks (`mod-overlay on`). Scripts change the world only through commands, so reading the state or drawing keeps the world
  hashes (`tests/l0_mod_script_state.txt` replays a reference with a mod reading everything).

## Actions and key bindings
The engine's hotkeys are named actions (`src/formats/key_actions.h`); `mod.json` may declare `actions` (a press calls `on_action(id)`),
move actions with `bindings`, or take an engine action over with `takes_over: ["action.<name>"]` (the same owner tracking as the
areas, `presentation_scripts.cpp` `SyncAreas`). The player's `[Keys]` section of `openbeach.ini` beats every mod. Conflicts are settled
by `key_actions::Resolve`, logged at start-up and offered to the mods screen as `mod_pack::KeyConflicts`. A mod that stops gives its keys
back (the table is rebuilt from the running mods). Nothing here reads the simulation or is recorded in saves and replays. Reference:
[actions.md](../../modding/reference/actions.md); the input side: [input.md](../input.md).

## Saves and replays
- With no active mods nothing changes: saves have no `mods` key, replays no `mods` line, and the world hashes and goldens stay.
- Only world mods are listed ([saves.md](../data-formats/saves.md#mods), [commands.md](../commands.md#replay-file)).
- A presentation mod's commands are recorded like the GUI's. Replaying a file while a mod that sends commands is active fails:
  during a replay the recording owns the input, and the script's commands are counted as ignored.
- Mod world scripts' state (state name, flags, variables) is stored in `world.mod_scripts`; `tests/replays/l0_world_script.replay`
  is recorded with the world mod `l0_world_script` and refused without it.
- `tests/replays/l0_mod_script.replay` is recorded with the presentation test mod `l0_script` and replayed without it
  (`tests/replay_l0_mod_script.txt`).

## Settings and the store
No setting in `openbeach.ini` changes the simulation. The per-mod store of the presentation scripts (`<game dir>/mod-data/<id>.json`)
lives outside saves and is never read by the simulation.

**Settings schema.** `src/core/settings_schema.h` describes a setting once (toggle, choice or number, label, hint, default,
options) and turns the text of an ini file into one canonical value. The engine's own settings are described with it
(`start_settings::EngineField`), so the settings window steps its rows through the schema, and a mod's `settings` in `mod.json`
parse into the same type (`mod_pack::Manifest::settings`), so the later mods screen (#118) draws both kinds of rows from one
description. A mod's values live in `openbeach.ini` under `[Mod.<id>]`; the game's side is `presentation_scripts.cpp`
(`ScriptHost::ReadSetting` reads them, `SetModSetting` is what the mods screen calls: it checks the value against the schema, writes
the file and runs `ScriptRuntime::SettingsChanged`, which calls `on_setting` for each value that changed). Scripted test runs
keep a changed value in memory and leave the player's file alone. The guard of the rule is
`tests/golden_hash_l0_settings_s1.txt`: the L0 golden run with every engine setting off its default must give the golden hashes.

## The mods screen and the engine's own layer
- **Own GUI layer** (`src/game/ui/own_layer.h/.cpp`, pure): sits above the pack and under every mod, passed as leading patches to
  `gui_patch::ApplyPackPatches`. For `main_menu` and `reset_menu` it adds an `OwnButton` (`id="mods_button"`) to `MainMenu` and
  the `ModsScreen` dialog (`ModsPanel`). If a pack's menu does not fit, the layout falls back to loading without the own layer.
- **Own widgets** (`src/game/ui/own_widgets.h/.cpp`): `GuiOwnButton` adopts a text-labeled button sprite from the active `.gui`
  layout at runtime when one is available (falling back to a self-drawn frame otherwise), and `GuiModsPanel` draws itself with
  plain fills and the layout's fonts, so no pack art is shipped and both work on the converted pack and on L0. Their texts come
  from `own_texts::ModsScreenTexts` (`src/game/game/own_texts.cpp`).
- **List and conflicts** (`src/game/mods/mod_list.h/.cpp`, pure): `ModList` tracks installed mods (`mod_pack::ListInstalled`),
  load order, per-mod problems (`Invalid`, `WrongPack`, `NeedsApi`, `MissingRequirement`, `RequirementLater`, `ConflictsWith`) and
  pluggable `ConflictSource` callbacks. `data_patch::FindConflicts` supplies value-override conflicts without publishing,
  `mods::FindTakeOverConflicts` supplies `takes_over` (input/camera/`action.*`) conflicts, and `mods::FindKeyConflicts` supplies
  key-binding conflicts from `mod_pack::KeyConflicts`.
- **Live apply** (`src/game/ui/mods_screen.cpp`): writes `[Mods] Active` to `openbeach.ini` (skipped during scripted test runs).
  Presentation mod changes apply at once (`mod_pack::ActivateList`, `mods::RestartPresentationScripts`,
  `mods::ConfigureKeyBindings`, `game_api::ReloadFrontEnd`); world mod changes wait for the next start and the footer says so.
  Settings step through `settings_schema::Field` and call `mods::SetModSetting`.
