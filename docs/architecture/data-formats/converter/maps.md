# Map conversion

`obl-convert maps <installation> <out> [--schemas <dir>]` writes the islands referenced by the installed scenario
PIS files under `<out>/maps/`, plus shared water animations and `props.json`. It reads campaign and sandbox references
without converting scenarios, campaign order, scripts or placements. It accepts either supported installed executable build;
no executable addresses or build-specific tables are needed. CD roots must first be extracted to an installation directory.
Converted content stays outside git.

## Effective map data

The converter follows the engine's PIS parser: ASCII case-insensitive keys, last entry wins, wrong-type entries hide
earlier entries. Installation paths resolve without regard to ASCII case on Windows and Linux; backslashes are
normalized. The map id is the lowercase BIZ stem (`scenario1` .. `scenario12`). References sharing an id must agree
on effective metadata and source paths; conflicting references fail instead of choosing one. JSON is validated with
the embedded schemas (or the explicit override) before writing it. An error can leave earlier outputs; no pack
manifest is written by this command.

The 128 x 128 grid is transposed from column-major words into an indexed PNG with this fixed legend:

| Index | Type | Source flags |
|---|---|---|
| 0 | `grass` | `0x80000001` |
| 1 | `sand` | `0x80000002` |
| 2 | `shallow_water` | `0x40000004` |
| 3 | `deep_sea` | `0x20000008` |
| 4 | `shoreline` | `0x100` |
| 5 | `rock` | `0x20` |
| 6 | `path` | `0x80` |

These names are the format's legend, not a new interpretation of simulation behavior. `path` is ground that only
objects with `build_on: path` may use. The converter preserves the
source grid, including border cells; the loader must still apply `ConstructGrid`'s border overwrite to `0x20` and
its `0x100` override. Unknown words and incorrect file lengths fail. The palette is for editing; the PNG indices
carry terrain identity.

Height PNGs contain the engine TGA loader's top-down **sample bytes**, not RGB palette values. Missing TGA files mean
flat terrain with `height_scale: 0`, as in `ConstructGrid`. The map JSON's `height` key is written exactly when `height.png` is, and
the engine reads that key (`map_pack::HasHeightMap`): a declared map without the file stops the game, as missing terrain does. Existing TGA files must be indexed 8-bit, use a 24-bit
palette and the origin the engine supports. Height dimensions stay those of the source image. Effective scales are
stored as float-round-tripping numbers; depth offsets are truncated integers with the engine defaults (plane 0,
slant 16).

Every BIZ block is range-checked and decoded with a bounded RefPack reader, including literal, overlapping copy and
stop commands. The file tables are `[y][x]`; each block holds row-major RGB565 words followed by depth words. Whole-map
`color.png` expands 565 by bit replication; `depth.png` is genuine 16-bit gray with PNG's big-endian samples.
No channel precision is lost. Water masks are range-checked signed runs, then a gray 1920 x 512 sheet: 15 frames
left to right, four 128 x 128 masks top to bottom, 7.5 frames per second. Gray 255 means an on mask, 0 off.

## Prop keys

`maps/props.json` (`obl.map_props`, L1) has `props`, an object from save-name content keys to arrays of catalog
scenery keys. It includes catalog display names and aliases used by `Create Scenery` in installed scripts. The
converter scans those commands with the engine's token boundaries and final-name rule; it does not move, execute or
write any placement or scenario logic. Unknown definition keys or empty names fail.

`ContentKey` is shared with the save converter: lowercase ASCII, each internal run of characters outside
`[a-z0-9_]` becomes one underscore, leading/trailing runs disappear. A display name such as `Palm 4s` thus becomes
`palm_4s`. Several definitions can share a script alias; arrays retain their candidates without losing an entry.
The save's `created_as` key selects the constructor definition when it differs from the saved `type` key. A future
script converter must use this lookup and retain command order and placements in the script.

## Verification

`convert_map.*` covers grid transposition, unknown flags, palette-independent height samples, exact indexed/16-bit
PNG output, RefPack command bounds, water sheet axes and malformed ranges, and prop aliases against the engine's
Create parser. `data.convert_maps_match_engine_loaders` converts all installed references in a unique
temporary directory and checks all twelve islands: terrain against `ConstructGrid`, heights against `DecodeTga`, every
color/depth pixel read by `map_pack::ReadIslandPlanes` against the installed `.biz` blocks decoded with the RefPack decoder, and every water frame against `WaterAnim`.
The test validates JSON, compares effective map settings with the engine PIS header reader and checks prop aliases
against the engine's full script parser when its vocabulary is available. It uses the user's installed files, skips
when maps are absent, and removes its outputs after success. `OBL_MAP_ORACLE_PACK` can point to a pack already written
by the CLI (for example, the Linux converter) to verify its files without converting again; supplied outputs stay intact.
`data.map_terrain_matches_installed_grids` compares the terrain the engine reads from `packs/base/maps/<id>/terrain.png`
(`map_pack::ReadTerrainWords`) with every installed scenario grid, word for word. `data.map_height_matches_installed_tgas`
checks that a map has `height.png` exactly when its installed height TGA exists, and that the samples equal the TGA's pixels
(`map_pack::ReadHeightSamples`). The engine reads terrain, heights, the island colour and depth planes and the water sheets from the pack. `water_anim::LoadSheet` re-encodes a sheet as signed runs (at most 127 bytes each), so `data.convert_maps_match_engine_loaders` also checks that the mask `Update` shows for every frame equals the one from the source `.bin`.
