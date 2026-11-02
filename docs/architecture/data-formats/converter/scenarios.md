# Scenario and campaign conversion

`obl-convert scenarios <installation> <out> [--schemas <dir>]` writes
`scenarios/<id>/scenario.json` for `s1`..`s12` and `sandbox1`..`sandbox12`, then
`scenarios/campaign.json`. Every output is validated before writing. The command uses installed data only,
without executable tables, and works with either supported build. Converted content stays outside git.
Run `scripts`, `maps`, `catalog`, `guests`, `sprites`, `images`, `texts` and `gui-layouts` into the same pack to
supply its references; this command does not duplicate those assets or load the new formats into the engine.

## Effective fields

The converter uses the engine's PIS parser: ASCII case-insensitive keys, last entry wins, and a wrong-type entry
hides an earlier value. Source paths resolve case-insensitively on Windows and Linux. Missing required headers,
unknown guest sets, unsupported catalog/path tile sources, empty consumed strings, unknown available building keys and invalid schema
values fail. Referenced BIZ, script, guest and catalog files must exist; scripts/guest/catalog names undergo the
engine's `.pis`/`.lgc` to `.ble` replacement. A failure can leave earlier files; no manifest is written.

| Engine input | Output |
|---|---|
| `SCENARIO_OBJ_TITLE`, `SCENARIO_OBJ_DESCRIPTION`, `SCENARIO_OBJ_OBJECTIVES` | `title`, `description`, `objectives_title`, preserving text-key case |
| `WORLD_STATUS_TITLE/AREA/TEMP/WEATHER/HAZARDS` | `status_texts.title/area/temperature/weather/hazards` |
| `STARTMAPX/Y` | `start_view`, truncated toward zero |
| `WORLD_MAPFILENAME` | Lowercase BIZ stem in `map`; map metadata remains in `maps/<map>/map.json` |
| `WORLD_BUILDINGLIST`, `WORLD_PATHTILEINFO` | Shared `catalog/index.json` and the catalog's path tiles; only the installed standard sources are supported |
| `WORLD_GUESTLIST` | `campaign_01`..`campaign_12`, `sandbox_easy/medium/hard`, selected from the actual reference |
| `BUILDING_<n>` | Lowercase `available_buildings`, keeping order and duplicates, up to 128 entries |
| `SCENARIO_OBJ_NUMBER`, `SCENARIO_OBJ_<n>` | `objectives`, keeping text order; absent/wrong-type count means zero; counts outside 0..4 fail |
| `MESSAGE_<n>` | `messages`, feedback types suppressed in this scenario |
| `WORLD_PAVEMENT_TILESET`, `WORLD_BEACH_EDGE_TILESET` | Lowercase optional `tilesets.pavement/beach_edge` |
| `RADAR_IMAGE`, `RADAR_POSX/Y` | Optional lowercase `radar.image` and truncated `radar.pos`; absent/wrong-type numbers default to zero |
| `PROGRESS_DIALOG` | Optional `progress_dialog`, preserving the GUI dialog's case |
| `WORLD_SCRIPT` | `script: world.wsc`, emitted separately by `scripts` |

Building/message lists stop at the first missing or wrong-type entry. CLI diagnostics list each unused entry
in source order, including shadowed duplicates and list entries beyond a gap or the building limit. Consumed
map fields are represented by the map asset, not listed as unknown. `WORLD_NAME` is required by `ReadHeader` but
is not stored or used by the current world constructor; the new `id` comes from the numbered playable setup.

The menu generates `MAINSS_SCENARIO<n>_TITLE/DESC` and `MAINSS_SANDBOX<n>_TITLE/DESC`; these become
`menu_texts`. The running sandbox title uses its menu title, while campaign gameplay uses `title`.
`pictures.large` references `ui3_fe_is_pics_large`, and `load/save` reference `ui3_fe_is_pics_small`,
all at frame `n - 1`. These are sprite frames, not separate screen PNGs. The schema still accepts image paths
for own content. Objective logic and prop placements remain in `world.wsc`.

## Campaign behavior

No installation file holds a campaign order or unlock rule; the values below are our own campaign definition, kept
compatible with the engine so progress and tests remain valid.

Campaign and sandbox entries are numeric order 1..12, matching the engine's numbered file construction and
zero-based selection index. `unlock: sequential` has `initially_available: [s1, s2, s3]` and
`always_available: [s1, s2]`, matching `game_core::Reset`. Completing n unlocks n+1 without removing those
exceptions. `sandbox_unlock: campaign_completed_or_always` matches `game_core::Selectable` in sandbox mode;
it must be evaluated against campaign progress, not sandbox play results. Cheats remain engine actions.

`scenario_screen` references `images/screens/scenarioscreen.png` from `images`. There is no per-scenario
`completion_screen` in the installed game's behavior. `ScenarioWon` is a script-selected GUI dialog;
`Completion0`..`Completion8` are the separate GameWon credits pages, already preserved by `gui-layouts` and
`images`. Conversion deliberately leaves the optional campaign completion-screen field absent.

## Verification

`convert.scenario_*` covers PIS precedence, wrong types, truncation, gaps, duplicate buildings, sandbox
references and refused inputs; `convert.scenarios_*` runs `ConvertScenarios` on a synthetic 24-scenario `MemorySource`
(documents, campaign order, ignored-entry report, missing references and unknown buildings). `data.convert_scenarios_match_engine_loaders` compares all 24 documents with
the engine's `ReadHeader`, `ReadTexts`, `ReadMessages` and `ReadBuildings` using the installed catalog IDs;
it also checks radar, tilesets, progress dialogs, menu references, sprite frames, campaign order and initial
availability against `game_core::Reset/Selectable`. Missing installed data skips the oracle.

`OBL_SCENARIO_ORACLE_PACK` points the same oracle at an existing CLI output, allowing a Windows engine check
of Linux-generated files. Otherwise it converts to a unique directory under `run/tmp/` and removes it on success.
The engine's campaign order is read from `campaign.json` by `scenario_load::LoadCampaign` (the new game's progress and the menu's
scenario ids), checked by `data.campaign_load_matches_engine`. The engine loader is `src/game/world/scenario_load.*` (`scenario_load::LoadPack`): it reads `scenarios/<id>/scenario.json` and the
`maps/<map>/map.json` it names, validated, into a `Setup`. The engine paths (BIZ, script, height map, grid, water animation) are derived
from the map id as the source `.pis` spells them; a sandbox's script folder is `sandscenario<n>` (it plays on its campaign's island).
`data.scenario_load_matches_engine` checks the loader against `pis::ReadHeader/ReadTexts/ReadMessages/ReadBuildings` for all 24
scenarios, including `m_MapName` byte for byte. The engine builds its world from the loaded `Setup` (`ConstructWorld`); the
scenario `.pis` files are not read at all (the game tests run with them moved aside). Building keys are the catalog's canonical keys.
