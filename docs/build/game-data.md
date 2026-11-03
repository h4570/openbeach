# Game data

The project contains no game assets. It runs on data converted from the user's own copy of Beach Life, an installation
or a CD ([../architecture/data-formats/sources.md](../architecture/data-formats/sources.md) lists the supported copies).

## Directories (all git-ignored)
- The user's own copy (digital release or CD), kept outside the working directory and **never modified**. The `data.`
  tests that need it find it through `OBL_OGDATA` and skip without it.
- `run/`: the game directory the game and tests use. The game writes its ini, saves, log and captures here.
- `run-slots/<n>/`: per-slot copies the parallel test runner builds from `run/` with hardlinks
  ([../testing/test-runner.md](../testing/test-runner.md)).

Create the pack with `obl-convert import <installation or CD root> run/packs/base`; the browser build imports the same
way ([../architecture/browser-build.md](../architecture/browser-build.md)).

Audio uses SDL; `tests/samples_no_miles.txt` asserts that no Miles library loads.
Recording metadata in GUI `environment.json` is not loaded as configuration.

## Converted pack in `run/packs/base/`
The game reads only converted data. The contract for every loader:
- **Location.** The converted pack is `<game dir>/packs/base/` with the pack layout of
  [principles.md](../architecture/data-formats/principles.md#packs): `rules/`, `catalog/`,
  `gui/`, ... For development `<game dir>` is `run/`. The game and `obl-sim` read it relative to
  the game directory, which is their working directory (script paths are relative the same way).
- **Producers.** `obl-convert` writes it from the installation in `<game dir>` itself, one command per format, always into
  `<game dir>/packs/base`. `obl-convert import` runs them all. Two other callers run the same commands and must stay in step when a loader is added:
  the post-build step of `obl-convert` (`cmake/convert_gui_tables.cmake`, refreshes an existing `run/` whenever `./build.ps1`
  relinks `obl-convert`, so a new loader's converter change reaches every checkout; Windows build only:
  `OBL_CONVERT_ON_BUILD` is off in the Linux portable configure) and `tools/obl_sim_linux.sh` (Linux `obl-sim`). A custom
  `--game-dir` needs the same commands.
- **Layers.** Loaders never build `packs/base/...` themselves: they ask `pack_path::Resolve(pack, "<logical path>")`
  (`core/pack_path.h`; `ResolveIn` for a loader handed one subdirectory such as `sprites/`). The first existing regular
  file wins: 1. `<game dir>/mod/<logical path>` (user and mod files shadow), 2. `<game dir>/packs/<id>/<logical path>`,
  the pack `pack_path::Selected()` names (`OBL_PACK`, default `base`; maps-scenarios.md "The L0 scenario").
  Neither existing yields the pack path, so the error names the converted file. Resolved so far: text, scripts, the object and world script
  vocabularies, catalog,
  guests, scenarios and campaign, game parameters, GUI tables and layouts, fonts, maps (terrain, height, colour, depth,
  water), sprites, images, the intro movie, the sound script (`sounds/sound_script.json`), the samples (a sample bank
  reads `mod/sounds/` and `packs/base/sounds/`, a name in the mod layer shadows the other), the soundtrack tracks and the
  night stream (`media_pack.h` `TrackDirectory`/`TrackPath`; a saved playlist keeps the `packs/base/music/` name, the
  file is looked up in the layers when it is opened, read for its ID3 tag or its length, and checked). Unit tests:
  `pack_path_test`, `media_pack_test`, `sound_bank_test`. Music and the intro use the selected pack too; the saved playlist
  directory is a logical alias, not a request to open the base pack. Sprite enumeration merges both layers in key order,
  including mod-only sprites and listing shadowed names once. Save conversion and `obl.save` loading resolve catalog
  and text files through the same layers. `pack.json` remains the selected pack's identity and hash, read directly from
  that pack (also at startup); a mod does not replace its identity. Missing assets never fall back to another pack.
- **No fallback.** A loader reads only the converted file; a missing or refused file stops the game with the file name
  and "Import the game data again". There is no switch to the source file.
- **Same engine state.** A loader's `data.` unit test builds the engine's structure from the source file and from the
  converted one and compares them.
- The producers also write `catalog/` (`obl-convert catalog`), `guests/` (`obl-convert guests`) and `text/` (`obl-convert texts`), then `pack.json`
  (`obl-convert pack`) last, since it hashes every file before it; `obl.save` loading reads all three
  ([save-load-bridge.md](../architecture/data-formats/converter/save-load-bridge.md)). Nothing under `packs/` is ever committed (`check_content.py`).

| File | Command | Loader |
|---|---|---|
| `catalog/` | `obl-convert catalog <game dir> <game dir>/packs/base` | `catalog::LoadPack` (`game/tables/catalog_load.cpp`), called from `WorldParams_Construct` |
| `guests/<set>.json` | `obl-convert guests <game dir> <game dir>/packs/base` | `guest_set::LoadPack` (`game/tables/guest_set_load.cpp`), called from `WorldParams_Construct` after the catalog, for the set `guest_sets::SetForList` maps the scenario's `WORLD_GUESTLIST` to (`game/objects/guest_sets.h`) |
| `catalog/`, `text/`, `pack.json` | `obl-convert catalog`, `texts`, `pack` `<game dir> <game dir>/packs/base` | `QueueOblSave` (`game/game/save_load.cpp`) |
| `rules/game_parameters.json` | `obl-convert game-parameters <game dir> <game dir>/packs/base` | `GameParameters_Construct` (`game.cpp`, `game/game/game_parameters_construct.cpp` `FromJson`) |
| `scripts/object_vocabulary.json`, `scripts/world_vocabulary.json` | `obl-convert scripts <game dir> <game dir>/packs/base` (state and message tables of `BeachLife.exe`) | `ParseScriptIds` (`game/objects/script_program_ids.cpp`) from `LoadScriptProgramVocabulary`, `ParseWorldScriptIds` (`game/world/world_script_load_ids.cpp`) from `LoadWorldScriptVocabulary`; both add the engine's script API (functions and values) |
| `scripts/feedback_vocabulary.json` | the same `scripts` step (notice keys, scenario message-type words, missing-staff notices, hire professions) | `ParseVocabulary` (`game/feedback/feedback_vocabulary.cpp`) from `LoadFeedbackVocabulary`; a missing file stops the start |
| `gui/object_browser.json`, `gui/credits.json` | `obl-convert gui-tables <BeachLife.exe> <game dir>/packs/base` | `gui_content::LoadPack` (`formats/gui_content.cpp`), called from `game/ui/gui_content_runtime.cpp` |
| `scripts/object/<name>.obs`, `scenarios/<id>/world.wsc` | `obl-convert scripts <game dir> <game dir>/packs/base` | `script_pack::ReadPackScript` (`src/formats/script_pack.cpp`), called from `ScriptProgram_Load` (`game/objects/script_program_load.cpp`) and `WorldScript_Load` (`game/world/world_script_load.cpp`); the Linux `obl-sim` takes the same path |
| `scenarios/<id>/scenario.json`, `scenarios/campaign.json` | `obl-convert scenarios <game dir> <game dir>/packs/base` (after `texts`, before `pack`) | `scenario_load::LoadPack` (`game/world/scenario_load.cpp`), called from `ConstructWorld` (`game/world/world_factory.cpp`) with the id the campaign gives the menu index; `campaign.json` is read by `scenario_load::LoadCampaign`, called from `Reset` and `NativeEnter` (`game/game/game.cpp`, `game/game/world_test_state.cpp`) (maps-scenarios.md "Campaign") |
| `gui/layouts.json`, `gui/<name>.gui` | `obl-convert gui-layouts <game dir> <game dir>/packs/base` | `gui_layouts::LoadLayouts` and `ReadText` (`game/ui/gui_layouts.cpp`), called from `game/ui/gui_layouts_runtime.cpp`: `GuiManager_LoadScript` (`game/ui/gui_manager_frame.h`) reads the layout a state names, with the file the language chooses |
| `fonts/<name>.json`, `fonts/<name>.png` | `obl-convert fonts <game dir> <game dir>/packs/base` | `font_pack::ReadFont` (`game/ui/font_pack.cpp`), called by `BitmapFont_Construct` (`game/ui/font_construct.cpp`); restores Unicode keys to the current byte-indexed glyphs and effective kerning, and PNG glyph bits to the atlas; a missing or invalid font stops the game |
| `maps/<id>/terrain.png` | `obl-convert maps <game dir> <game dir>/packs/base` (skipped while `maps/conversion.stamp` matches the installation's inputs) | `map::CreateGrid` (`game/map/grid_lifecycle.cpp`) through `map_pack::ReadTerrainWords` (`formats/map_pack.cpp`), id = the grid file name without `grid.dat`. `maps/<id>/height.png` (same id; the `height` key of `maps/<id>/map.json` says whether the map has one, a declared file that is missing stops the game, and a map without the key is flat) becomes the height Image through `map_pack::ReadHeightSamples`. `maps/<id>/color.png` and `depth.png` become the map cache planes (`MapCache::Construct` through `map_pack::ReadIslandPlanes`, `game/ui/map_cache.cpp`); `maps/water/<k>.png` becomes the water animation (`WaterAnim_Load` and its two Renderer/MapView wrappers through `map_pack::ReadWaterSheet` and `water_anim::LoadSheet`, `game/map/water_anim.cpp`; a missing sheet stops the game) |
| `sounds/sound_script.json`, `sounds/<name>.wav` | `obl-convert sounds <game dir> <game dir>/packs/base` | `ScriptedSound_Load` (`game/ui/scripted_sound_runtime.cpp`) through `scripted_sound::LoadDocument` (`game/ui/scripted_sound_pack.cpp`); a missing script stops the game. The samples: `SoundBank_Construct` (`game/ui/sample_mixer.cpp`) through `audio::SoundBank::Open` (`game/ui/sound_bank.cpp`); a directory without `.wav` files stops the game |
| `sprites/<key>.json`, palettes and sheets | `obl-convert sprites <game dir> <game dir>/packs/base` (skipped while `sprites/conversion.stamp` matches) | `SpriteBank_Load` (`game/ui/sprite_bank.cpp`) through `sprite_pack::ListSprites` and `ReadSprite` (`src/formats/sprite_pack.cpp`), every `obl.sprite` document in key order; the frames: `SpriteAnim_LoadAll` and `LoadDirection` (`game/ui/sprite_anim.cpp`) re-encode them from the sheets with `sprite_pack::EncodeFrames` (`SpriteAnim::m_dataOffset` is the index of the converted animation). Path tiles take their lookup and full frame from the selected pavement sprite's `autotile` table |
| `images/cursor.png`, `images/screens/<key>.png`, `images/haze/0..5.png` | `obl-convert images <game dir> <game dir>/packs/base` | `Tga_Load` / `Tiff_Load` (`game/ui/image.cpp`) through `image::PackPath` and `LoadPng` (`core/image_decode.cpp`). PNGs preserve engine pixel depth, palette indices and palettes; missing or rejected images stop the game |
| `music/<name>.mp3`, `video/intro.mpg` | `obl-convert media <game dir> <game dir>/packs/base` (byte copies of the soundtrack and the intro movie) | the playlist's built-in tracks (`settings_store.cpp` adds them unchecked; `Playlist_AddTrack` in `game/ui/playlist_playback.cpp` opens each one), the night stream (`game/game/game_states.h` `kNightStream`, opened by `game/game/game_states.cpp`), and the intro (`game/game/game.cpp`, `Movie_Play`); each open goes through `media_files::RequireWindowedFile` (`game/ui/media_files.cpp`): a missing file stops the windowed game with the "Import the game data again" hint, the headless run skips the check and boots without music or video. The check itself is `media_pack::CheckReadable` (`game/ui/media_pack.cpp`) |

## File access
Pack loaders resolve the files described above under the selected pack. Saves, settings and logs use the game
directory; imported asset files are converter inputs rather than engine inputs.

## Rule: no game data in git
Game assets, executables, DLLs, disc images, music, video, sprite archives and anything extracted from them stay out of
git: history cannot be shrunk later. Small derived test references (world-hash tables, a few golden images) are allowed
when they reveal no asset content.
