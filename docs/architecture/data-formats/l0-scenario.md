# The L0 scenario

The minimal own scenario uses exactly these formats in an own pack in git (`data/packs/l0/`, provenance
`own`): a small map (the loader accepts only 4096 x 4096 maps, so the L0 map is a mostly empty sea with a small
island), a few buildings with own numbers, placeholder sprites as `rgba` PNG, PCM WAV sounds, an English text table and
a short world script. It needs no L2 file in headless runs, so Linux CI runs scenario tests and a bot on it without the
encrypted archive.

**Pack selection.** Every loader reads `pack_path::Selected()`: `packs/<id>` under the game directory, `<id>` from
`OBL_PACK` (lowercase letters, digits, `_`, `-`), default `base`. The start stops (`InitGame` returns 8, the log names the reason) when the
id is invalid or an explicitly set pack has no `packs/<id>/pack.json`. The `mod/` layer is shared by every pack. `obl.save` loading
records the selected pack's identity. Still `base` only: music tracks, the night stream and the intro movie
(`media_pack.h`, `game_states.h`); they are L2 and the headless run skips them. The game reads `<game dir>/packs/l0/`,
so the producers install `data/packs/l0/` there (a copy, like the converted pack's files).

**What a headless start of an L0 scenario reads** (from the loaders in `docs/build/game-data.md`; "verify" marks what the
first `OBL_PACK=l0` boot must confirm):

| File | Needed | Notes |
|---|---|---|
| `pack.json` | always | start check; `provenance: own` |
| `rules/game_parameters.json` | always | own values |
| `text/en.json` | optional | a missing table is empty; the engine draws by key |
| `fonts/<name>.json` + `.png` | always | read headless too (the atlas stays blank there); the GUI constructs `guifont7`, `guifont8`, `guifont8b`, `guifont12b`, `guifont14b` and `photo16` (the `pol` variants only for Polish). Keys are the Windows-1252 code points of the byte-indexed glyphs (five bytes have none), `kerning` must exist (may be empty), atlas pixels are 0..3 with alpha 255 exactly where non-zero |
| `images/cursor.png`, `images/screens/<key>.png`, `images/haze/0..5.png` | always | the boot loads the cursor (256 x 256 indexed), haze 0..5 (32 x 24 indexed) and the 800 x 600 indexed screens; a missing one stops the game. L0 ships all 17 screen keys |
| `sprites/<key>.json` | always | headless reads only metadata (no sheets); a missing name falls back to `dummy`, so `dummy` is required, plus the pavement sprite with its `autotile` table (path tiles) and the animations whose frame counts drive the simulation (guests, staff, buildings) |
| `sounds/sound_script.json`, `sounds/*.wav` | always | a missing script or a directory without `.wav` stops the game; the boot accepts `crowd`, one group and a few own WAVs |
| `sprites/palette_modifiers.json` + `.png` | always | exactly 12 tables of 64 colours (three tone groups of four); the boot stops without it |
| `gui/layouts.json` + `gui/*.gui` | always | the five layouts `in_game`, `intro`, `main_menu`, `reset_menu`, `game_won`; own menu buttons have readable captions baked into `l0_menu_*` sprites; `in_game.gui` supplies the HUD, play panels and scenario progress dialog |
| `catalog/index.json` + its documents | at world start | the engine's vocabulary (desires, commodity families, opening hours, staff kinds) keeps its keys, values are ours; only capacities are upper bounds; a few own buildings |
| `guests/<set>.json` | at world start | the set the scenario names; guest types with own sprites |
| `scripts/object/<name>.obs` | per spawned kind | L0 uses own guest and builder scripts. Buildings need no object script |
| `scenarios/campaign.json` | always | 1 to 12 entries per list, equal counts; L0 has one campaign and one sandbox entry, id `l0` |
| `scenarios/<id>/scenario.json`, `world.wsc` | at world start | own short world script |
| `maps/<id>/map.json`, `terrain.png` | at world start | 4096 x 4096 only; `height.png` optional |
| `maps/water/<k>.png` | at world start | read headless too, although the doc calls it L2: an own mask sheet or a headless skip |
| `maps/<id>/color.png`, `depth.png` | windowed only | headless skips them |

`tools/gen_l0_pack.py` writes the images, sprites, sounds, palette tables, fonts, the map, the catalog, the scenario, the world
script, its vocabulary, the guest set, media, the play GUI (`write_play_gui`) and the menu tables and labels (`write_menu_gui`:
`gui/object_browser.json` with empty lists, `gui/credits.json` with one final row that our own credits block follows, and the
L0 texts of the Load and Reset dialogs); the other GUI files (`main_menu.gui` with its CreditsScreen, LoadGameMenu and
ResetProgressConfirm dialogs) and `campaign.json` are hand-written in git. Both tables are required: the engine's FatalError
on a missing one is what stopped Credits and Load before. `tests/l0_menu_actions.txt` (`@hidden`, `OBL_PACK=l0`) opens Credits,
Load and Reset in the hidden window, answers Reset with "no" and cancels Load.
Media generation requires `ffmpeg` on PATH: `write_media` encodes four seconds of synthesized digital silence to each
of the twelve built-in day/night MP3 keys (including the separate `night6.mp3` stream), and a one-second solid blue
320 x 240 MPEG-1 intro without audio. These are own placeholders, built from no imported input. `play_movie` is enabled in
the L0 parameters; normal starts play the intro, scripted starts skip playback unless `OBL_PLAYMOVIE=1`.
`tests/l0_media.txt` runs hidden and windowed with playback enabled, checks all 25 intro frames, decoded silence in a
day/night playlist and the GameWon state's separate night stream, then exits cleanly. It also passes with
`run/packs/base` moved away (restore that directory in a `finally` block after the test).
`tests/l0_boot.txt` (`@headless`, `OBL_PACK=l0`) boots to the main menu state; `tests/l0_gameplay.txt` (`@headless`) boots the
scenario, checks the script's cash and its rep office, places an own building from the catalog and steps. A missing or refused
file is a `FatalError` and fails them; both pass with `run/packs/base` moved away.

**What the World item settled** (traps the first `boot-gameplay` hit, in the order it hit them):
- **Ids.** The scenario is `s1` (map `scenario1`, guest set `l0`): the script path the engine builds is
  `Data/Scenario/scenario<n>/...` and maps back to `scenarios/s<n>/world.wsc`, and a map id is `scenario<n>`. `campaign.json`
  lists `s1` in both lists (one entry each; the sandbox shares the scenario).
- **Map.** `terrain.png` is 128 x 128 with the base legend (the engine overwrites the border); `map.json` needs `color` and
  `depth` (schema), so L0 ships a flat 4096 x 4096 colour and depth too (a few KB each; headless skips them). The water sheet
  `maps/water/1.png` is own dots, 15 x 4 masks.
- **GUI.** `scenario.json` needs `radar` and `progress_dialog`: a missing one is read as a null name and crashes
  (`strlen(null)` in `CreateRadar`/`SetProgressDialog`). The first rendered missing-dialog probe was `BuildPanel`.
  `in_game.gui` now has the build palette, cancel-placement dialog, provider-backed `L0Building` panel (shared by the
  three public buildings and REP), goals, camera controls and HUD for both 800+ and 640-pixel widths. LOO/HUT use the leisure category,
  KIOSK food: the building image lists support categories 0..2, so `other` buildings cannot be selected there.
  All play graphics are own indexed `ui` sprites; `special` sprites draw into the world buffer rather than GUI surfaces.
  The HUD surface is anchored at the bottom (94/75 rows); HUD pixels use `B` coordinates to stay inside it at any height.
  The GUI parser requires a
  space after the bare `MainItem`/`MainItem640` token even with LF files. Fonts use own hand-drawn 5 x 7 capitals, digits and punctuation, scaled to each font size.
  Lowercase renders as capitals; unsupported extended characters use a question mark.
  Menu captions instead use hand-drawn capitals baked into four-state `l0_menu_*` art (`write_menu_sprites`).
  The HUD paints all 88/70 reserved rows: the world does not redraw that band, and a partial transparent HUD left
  scenario-screen buttons and the loading gradient there even after the dialog entries were destroyed.
  `tests/l0_menu_transition.txt` follows the real menu start, checks the empty dialog list, readable button pixels,
  the entire HUD strip and a working build button (hidden windowed run).
  The small green square near the map's top left is the radar camera rectangle, not the cursor: `CreateRadar`
  uses the scenario's `dummy` image at `[9, 0]`, and `DrawRadar` draws `m_colorViewRect` in green `(0, 250, 0)`
  over that 40 x 78 placeholder. A proper own minimap remains future content work.
  `tests/l0_play_gui.txt` is the hidden rendered click-through test: build/cancel, building close/open/follow, goals,
  zoom and photo capture. It selects English and clears the INI playlist so a remembered base-pack language or music
  playlist does not contaminate an L0-only run. It also passes in a game directory with only `packs/l0`, an INI and the
  launcher's `remaster/` runtime files (no `Data`, `Scenario`, `sp.bin` or base pack).
- **Sound script.** `sprite_sounds` names the sprite's animations (`dummy` has one, `dummy`); `building_sounds` and
  `opinion_sounds` must exist, may be empty.
- **Catalog vocabulary.** The engine finds families, commodities, desires and opening hours by their key,
  so the index lists the complete engine vocabulary (24 families, 41 commodities, 14 desires, 5
  opening hours) with our names and groupings; events, beers, fines, tables and scenery are empty. The staff
  buttons look up the six staff homes by name (`BAYWATCH`, `HANDYMAN`, `REP`, `BUILDERYARD`, `CLEANERSHACK`,
  `SECURITY_POST`), so they exist as small stub buildings the scenario does not offer. The build tree: a building without
  `requires` is "locked" (`NONE`), so the three own buildings (`loo`, `kiosk`, `hut`) require `REP`, which the script creates.
  `LOO` is also the engine's key for the toilet behavior.
- **World script vocabulary.** `scripts/world_vocabulary.json` (108 messages, 9 functions, 35 values) is written by the
  generator: message ids from our `MessageType` enum, functions and values as literals (the language of the engine). The
  object vocabulary `scripts/object_vocabulary.json` (format `blre-script-ids`) lists all 108 message ids from the enum and
  only the states, functions and values the own guest and builder scripts name (the ids are the engine's, so
  `ParseScriptIds` asks one row for those tables). The world script loads it with `LoadScript GuestLogic`.
- **Builders and the goal.** The world script creates a `BUILDERYARD` whose definition lists two `builder` staff (own
  `catalog/staff/builder.json`); they spawn with it and run the own `scripts/object/builder.obs` (`LoadScript
  BuilderLogic`): rest and call `ReallocateBuilders`, on `MT_CONSTRUCT` walk straight to the site, call `RemoveCostPoint`
  every update until `MT_CONSTRUCTION_FINISHED`, rest again. `AtWork` gates allocation, walking and work: a shift end
  releases the job, resting polls for a new shift and resumes it. `MT_DEMOLISH` also walks to the assigned site,
  then `AmIDemolishing` selects state 100; demolition uses the same work-point function. `tests/l0_builders.txt`
  interrupts and resumes both jobs, observes the demolition state and checks removal and the 225 refund. The L0Building
  panel supplies Sell and its ConfirmDemolish dialog (also required by the typed demolition command).
  `STATE_WORLD_RUNGAME` wins on `BuildTreeExist` plus `IncomeToday >= 1` (`ShowMenu ScenarioWon L0_WON`) and loses at
  17:00 on script day 1. `tests/l0_goal.txt` proves finished buildings cannot win without visitor sales;
  `tests/bots/l0/build_tree.bot` finishes the buildings and waits for sales with the `build-tree-s1` bot
  ([ci.md](../../build/ci-l0.md)); bands recorded with seeds 1–3 finish at ticks 864–865.
  Arrival previously never happened because the world script defined only a route, no `BoatEvent`, and the catalog
  had no arrival vehicle. It now schedules six guests at 14:00 on day 0, independent of rating. The arrival host chooses
  `BUS` when no `WHARF` exists, so L0 has an own shuttle definition and a short route on walkable island tiles.
  The route uses the simulation's staggered 64-unit tile geometry, not the map image's 32-pixel cells.
  A performance arrival at rating zero has a two-guest fallback in the ferry core; changing shared rating logic
  was unnecessary. `guest.obs` is unchanged by this follow-up.
- **Guest set.** `guests` names a set freely (`guest_sets::IsPackSetName`); the table of the fifteen names of the campaign only maps
  the lists the converter writes for it. `guests/l0.json` is one type on the dummy sprite and three name keys; it wants soft drinks (the kiosk, created by the
  world script because a spawned building stays a building site without builders) and a faint stroll desire (the engine
  refuses a guest without one). The island has no pavement, so `guest.obs` answers `MT_PATHBLOCKED` with a direct walk
  (`MT_MOVE2OBJECT`); guests arrive (`spawn-guests`), walk to the kiosk, are served and leave (`MT_KILLOBJECT` to the world,
  receiver -2, in `STATE_GUEST_LEAVING_WORLD`, which counts the departure). The kiosk has room for four guests at once, so
  the other two queue (`MT_JOIN_QUEUE` puts them in `STATE_GUEST_WAITING_DESIRE`). A guest leaving the building sends
  `MT_COMMOD_QUALITY` (data = its desired commodity, the slot) which frees its place and lets the next queued guest in
  with `MT_IM_READY`; without it the queue stalls with two guests stuck. `tests/l0_guests.txt` expects all 6 guests gone.
  The object vocabulary names the message's flags word `MessFlags` (value 43, the `flags` word the guest reads; `MessData` is
  `data`), the same name the base pack's scripts use. `MT_MOVE2QUEUEPOINT` (flags = x, data = y, truncated) is not handled
  by `guest.obs` yet: a handler that walks the guest there gave the same guest positions as no handler in a trial run, so
  the walk is not proven by position samples yet.

Test scripts still start scenarios by menu index (`boot-gameplay island=1` = `campaign[0]`), which is enough for one
L0 scenario. Provenance: the L0 files are written by us or generated by our scripts, never derived from converted
files; `check_content.py` must accept `data/packs/l0/` as `own`. The GUI tables (`gui/credits.json`,
`gui/object_browser.json`) carry `provenance: own` and their schemas accept it (`tools/gen_l0_pack.py` writes them).

`tests/tools/test_l0_fonts.py` checks regeneration, atlas pixels and text character coverage;
`convert.l0_fonts` loads all six shipped fonts with the production reader and checks the L0 text glyphs.
