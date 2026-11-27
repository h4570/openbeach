# Data formats: maps, scenarios, campaign

Map, scenario and campaign formats separate reusable terrain from scenario rules. Shared rules are in
[principles.md](principles.md).

## Maps
A map is separate from its scenarios, so several scenarios share one map (the source keeps the island inside the campaign
scenario directory, `ScenarioN/`):
```
maps/<map>/map.json
maps/<map>/terrain.png     128 x 128, indexed terrain types          L1
maps/<map>/height.png      8-bit gray, optional (flat when absent)   L1
maps/<map>/color.png       4096 x 4096 RGB (pre-rendered island)     L2
maps/<map>/depth.png       4096 x 4096 16-bit gray (occlusion depth) L2
```
```json
{
  "format": "obl.map", "version": 2,
  "size_px": [4096, 4096], "cell_px": 32, "cells": [128, 128],
  "event_area": [100, 200, 300, 400],
  "terrain": "terrain.png",
  "terrain_types": [{ "index": 0, "type": "grass" }, { "index": 1, "type": "sand" }, { "index": 2, "type": "shallow_water" },
                    { "index": 3, "type": "deep_sea" }, { "index": 4, "type": "shoreline" }, { "index": 5, "type": "rock" },
                    { "index": 6, "type": "path" }],
  "height": "height.png", "height_scale": 0.5,
  "color": "color.png", "depth": "depth.png",
  "water_anim": "water/1", "depth_plane_offset": 0, "depth_slant_offset": 0
}
```
- **Event area**: required `[min_x, min_y, max_x, max_y]` integer world coordinates, with ordered bounds. Event and
  shark test drivers read the selected map's area; missing or invalid bounds fail without a fallback. The converter writes
  the installation placement bounds; L0 defines its own area in `tools/gen_l0_pack.py`.
- **Terrain** (`<n>grid.dat`, 128 x 128 `u32`, column-major) becomes a palette-index PNG (row-major, viewable and
  editable in an image editor) plus the legend above. The types are our enum; the engine maps each type to its grid
  flags (grass `0x80000001`, sand `0x80000002`, ...), and the
  converter checks that every value of every map is in the mapping, so the grid in memory, saves and world hashes stay
  identical. An unknown value stops the conversion.
- **Height** (`<n>height.tga`, 8-bit with a palette, only some maps) becomes an 8-bit gray PNG of the sample bytes
  (the engine reads indices directly, not palette colors);
  `height_scale` is the scenario's `WORLD_HEIGHTMAPSCALE`.
- **Color and depth** (`.biz`: 32 x 32 blocks of 128 x 128 pixels, RefPack-compressed RGB565 color + `u16` depth) become
  two whole-map PNGs. Color is expanded by bit replication (lossless back to 565, [assets.md](assets.md#images-and-pixels)),
  depth stays 16 bits. `BizMap_Construct` decodes them at map load (`map_pack::ReadIslandPlanes`, 64 MiB for both planes,
  released by `BizMap_Close`) and the four-slot block cache copies its 128 x 128 blocks from them; the `.biz` file is no
  longer read. The headless host skips both files. A tiled layout for faster
  load or larger maps is not needed for 4096 x 4096.
- **Maps are pre-rendered images**: there is no terrain renderer, so every own map
  ships its painted or tool-generated `color.png` and `depth.png`.
- Map size, cell size and grid size are fields, not engine constants; the loader accepts only the current values.

`maps/props.json` (`obl.map_props`) maps save-name content keys to arrays of candidate catalog scenery keys. It includes
catalog display names and scenario-script aliases; placements remain in scripts. The save converter and map converter
share the lowercase/separator-collapse key rule; `created_as` distinguishes definitions that share one name.
Conversion and engine checks: [converter/maps.md](converter/maps.md).

## Water shimmer
`Data/WaterAnims/WaterAnim<k>.bin` (15 frames, each four 128 x 128 on/off masks) -> `maps/water/<k>.png` (a sheet of
15 x 4 gray masks) + `maps/water/<k>.json` (`obl.water_anim`: `sheet`, `mask_size`, `frames`, `masks_per_frame`,
`rate`). Shared by maps, chosen by
`water_anim`. L2 (it only changes pixels).

## Scenarios
One directory per playable setup, campaign and sandbox alike:
```
scenarios/<id>/scenario.json
scenarios/<id>/world.wsc       the scenario logic (game-data.md, "Scripts")
```
Ids are ours and match the replay header (`s1`..`s12`, `sandbox1`..`sandbox12`; [../commands.md](../commands.md#replay-file)).
```json
{
  "format": "obl.scenario", "version": 1,
  "id": "s3", "kind": "campaign",
  "title": "<text key>", "description": "<text key>", "objectives_title": "<text key>",
  "menu_texts": { "title": "<text key>", "description": "<text key>" },
  "status_texts": { "title": "<text key>", "area": "...", "temperature": "...", "weather": "...", "hazards": "..." },
  "map": "scenario3", "start_view": [1520, 2210],
  "catalog": "catalog/index.json", "guests": "campaign_03",
  "available_buildings": ["beachbar", "hotel"],
  "objectives": [{ "text": "<text key>" }],
  "messages": ["<text key>"],
  "tilesets": { "pavement": "<sprite>", "beach_edge": "<sprite>" },
  "radar": { "image": "<sprite>", "pos": [12, 34] },
  "pictures": { "large": { "sprite": "ui3_fe_is_pics_large", "frame": 2 },
                "load": { "sprite": "ui3_fe_is_pics_small", "frame": 2 },
                "save": { "sprite": "ui3_fe_is_pics_small", "frame": 2 } },
  "progress_dialog": "<gui dialog>",
  "script": "world.wsc"
}
```
- Every `.pis` key the game reads gets a named field; keys the game never reads are dropped by the converter and listed
  in its report. The `.pis` lookup rule (case-insensitive, last entry wins) is applied by the converter, so
  `scenario.json` holds effective values only.
- File names inside the `.pis` (`WORLD_MAPFILENAME`, `WORLD_BUILDINGLIST` with its `.pis`-to-`.ble` renaming,
  `WORLD_PATHTILEINFO`) disappear: the scenario references a map id, the catalog, a guest set and its script.
- The objective *logic* lives in the world script; `objectives` only lists texts and their order
  (`SCENARIO_OBJ_*`).
- `available_buildings` replaces `BUILDING_<n>`; order and duplicates are kept, stopping at the first missing or
  wrong-type entry and the engine's 128 slots. `messages` names suppressed feedback message types, also stopping at
  the first gap; these are not greetings.
- `objectives_title` preserves `SCENARIO_OBJ_OBJECTIVES`. The scenario menu reads the text keys
  `menu_texts.title` and `menu_texts.description`. Sandbox gameplay and newly written sandbox saves also use
  `menu_texts.title`. Campaign gameplay uses `title`. If `menu_texts` is absent, the engine uses `MAINSS_SCENARIO<n>_TITLE` and
  `MAINSS_SCENARIO<n>_DESC`, or `MAINSS_SANDBOX<n>_TITLE` and `MAINSS_SANDBOX<n>_DESC` for sandboxes,
  where `n` is the 1-based menu slot.
- `pictures.large`, `pictures.load` and `pictures.save` select the menu and save panel previews. Each can
  name a `{sprite, frame}` or a pack-relative PNG path. Active mods can override these assets. An omitted
  picture uses frame `n - 1` of `ui3_fe_is_pics_large` for `large` and `ui3_fe_is_pics_small` for `load` and `save`.
- `script` names the world script relative to `scenarios/<id>/`. It defaults to `world.wsc`. The engine looks
  up the file through the pack and active mod layers.

Conversion and engine checks: [converter/scenarios.md](converter/scenarios.md).

## Campaign
`scenarios/campaign.json` lists the campaign order (`campaign`: scenario and its `completion_screen`), the unlock rule
(`unlock`: `sequential` or `all`), the `sandboxes` and the `scenario_screen` of the menu. The converter writes the source's
order and our own packs write their own campaign. Optional `initially_available` and `always_available` list exceptions to
sequential unlocking. The installed game starts with `s1`, `s2`, `s3` selectable; `s1` and `s2` are always available.
`sandbox_unlock` is `all` or `campaign_completed_or_always`: the latter makes sandbox n selectable when campaign n
is completed or always available. No per-scenario `completion_screen` is emitted for this installation: its nine
completion images belong to the GameWon credits pages. A save names its scenario by id ([saves.md](saves.md)).

## Scenario sets
Mods can add extra campaign scenarios and sandboxes beside `scenarios/campaign.json` via `scenarios/sets/<set id>.json`
(`obl.scenario_set` v1; [../../modding/reference/scenarios.md](../../modding/reference/scenarios.md)).
- `ScenarioRegistry::LoadFromPack` discovers `scenarios/sets/*.json` over active mod layers (`pack_path::ListStems`),
  orders sets by mod load order then set stem, and appends extra entries after the pack's with `menuSlot = -1` so pack
  `ScenarioRef` indices never move.
- Each set id and every scenario/sandbox id it lists must start with the owning mod id (`-` written as `_`). Start-up
  and `--check-mods` refuse wrong prefixes, duplicate ids, missing `scenario.json`/map/script (listing available pack
  maps on an unknown `map`), and unknown `requires_completed` pack campaign ids.
- `ScenarioProgress` stores extra scenario progress in `progress.json` under the optional `"extra": { "<id>": <flags> }`
  object using the same flag bits (`1` locked, `2` unlocked, `4` always available, `8` completed). Entries of inactive
  mods are preserved when `progress.json` is written.
- A mod scenario's own `world.wsc` is parsed in strict mod mode: `UnlockNextScenario` and `ShowMenu ScenarioWon` /
  `ScenarioLost` are allowed, while `ShowMenu GameWon` is reserved for the pack campaign.

The engine reads the order from this file (`scenario_load::LoadCampaign`): the menu index (0-based) selects
`campaign[index]` or `sandboxes[index]`, the new game's progress table starts from `game_core::Reset` with the two
availability lists as menu indices, and a world loads the scenario id it names. The file must hold 12 entries of each list
(the progress table has 12 slots) and the availability lists must name campaign entries. The engine supports
`unlock: sequential` and `sandbox_unlock: campaign_completed_or_always` only; their unlock rules stay code in `game_core`
(`Selectable`, `Complete`), so a pack asking for another rule is refused.
`data.campaign_load_matches_engine` checks the file's order and start progress against the built-in order it replaced, and
`data.campaign_order_comes_from_the_file` checks that a reordered campaign drives the engine instead.

## The L0 scenario

See [l0-scenario.md](l0-scenario.md) for the own pack, generator, loader requirements and scenario tests.
