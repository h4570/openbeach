# Buildings reference

A building mod adds a record in `catalog/buildings/<key>.json`, registers `<key>` in `catalog/index.patch.json`, enables it in a scenario via `scenarios/<id>/scenario.patch.json`, and ships its indexed sprites, text entries, and optional sounds. For a step-by-step walkthrough, see [first-building.md](../guides/first-building.md).

## Record fields (`obl.building` v2)

Validated against `data/schemas/building.schema.json`. Required fields are marked **required**.

| Field | Type | Meaning |
|---|---|---|
| `format` | `"obl.building"` | **Required.** Format tag. |
| `version` | `2` | **Required.** Schema version. |
| `key` | key | **Required.** Unique catalog key; must match the filename `<key>.json` and the entry in `catalog/index.patch.json`. |
| `name` | string | **Required.** Text-table key (recommended) or literal display name. |
| `category` | `"hotel" \| "food" \| "leisure" \| "other"` | Build-panel tab (`hotel`, `food`, `leisure`); `"other"` is hidden from the build panel and excluded from unbuilt-building counts. |
| `behavior` | enum | Engine special-case dispatch (`"none"` for an ordinary shop). See [Behaviors](#behaviors) below. |
| `sprite` | key | **Required.** Sprite key in `sprites/<sprite>.json` (`kind: "building"`). |
| `icon` | integer >= 0 | Frame index in the icon sprite sheet (default `0`). |
| `icon_sprite` | key | Own sprite (`kind: "ui"`) for the build-panel icon and `BUILD_THUMB`; when omitted, uses the pack's `UI3_bp_thumb` at frame `icon`. |
| `icon_locked_sprite` | key | Own sprite when locked by `requires`; falls back to `icon_sprite`, then `UI3_bp_thumb_grey`. |
| `preview_sprite` | key | Own sprite for `BUILD_LARGE_PREVIEW`; falls back to `icon_sprite`, then `UI3_bp_preview`. |
| `menu` | string | Dialog name from `gui/in_game.gui` opened when clicking the building (e.g. `"L0Building"` on L0). |
| `requires` | string | Prerequisite building key in the build tree (e.g. `"rep"` on L0). Omitting it or setting `"NONE"` marks the building as not player-buildable. |
| `cost` | `{ build, refund? }` | **Required.** Build cost and demolition refund. |
| `construction` | `{ build_time?, demolish_time?, max_builders?, anim? }` | Build/demolish time, builder cap, and optional custom construction sprite key. |
| `size` | `[w, h]` | **Required.** Tile footprint dimensions. |
| `build_on` | array | **Required.** Non-empty subset of `"land"`, `"beach"`, `"shallow_water"`, `"deep_sea"`. |
| `baseplates` | array of int | Base plate types laid beneath the building. |
| `points` | `{ serve?, staff?, entrance?, walk? }` | Local offsets for guest service, staff, entrance, and interior walk points. |
| `operation` | object | `guest_capacity`, `opening_hours` (catalog key), `max_instances`, `max_queue`, `attractor` (commodity family key), `power` (negative = consumes, positive = generates), `staff: { types, max }`. |
| `commodities` | array (max 5) | Each entry: `{ commodity, intensity: [men, women], daily_intensity_factor, price, daily_price_factor, cost }`. |
| `failure` | `{ average_time?, percent?, age? }` | Breakdown interval, failure chance (0..100), and age threshold. |
| `vehicles` / `vehicle_places` | arrays (max 5) | Vehicle catalog keys and `{ dir, offset }` spawn positions. |
| `tables` | array (max 10) | `{ table, pos }` seating tables. |
| `events` | array (max 5) | Event catalog keys for stage/club shows. |
| `anims` | array (max 8) | Secondary animated sprites: `{ sprite, points: [[x,y] x 4], flag }`. |
| `staff_files` | `{ male?, female? }` | Text keys for staff names. |
| `footprint` | `{ size: [w, h], layers: { placement, work, depth, body } }` | `(h + 2) x (w + 2)` grids of placement mask, builder work points, actor depth lift, and body/surface bits. |

## Build panel and tooltips

The build panel (`BuildPanel` in `gui/in_game.gui`) automatically populates its `BUILD_HOTEL_SMALL`, `BUILD_FOOD_SMALL`, and `BUILD_LEISURE_SMALL` image lists from all buildings in the catalog whose `category` matches and whose key is listed in the active scenario's `available_buildings` (patchable via `scenarios/<id>/scenario.patch.json`).

For each building, the engine looks up `BUILDING_NAME_<KEY>` for the build-panel title and `BUILD_TIPTEXT_<KEY>` (both uppercase) for the icon tooltip in the text table.

## Behaviors

`behavior` is a closed list in `src/game/tables/building_behavior.h`. An ordinary shop or attraction needs only `"none"` plus `commodities` and `operation.attractor`. Pick another value only when you want that engine behavior:

| Value | Effect |
|---|---|
| `none` | Standard building driven purely by `commodities`, `operation`, `tables`, and `events`. |
| `hotel` | Accommodation income, power-exempt, primary guest lodging. |
| `accom_block` | Accommodation income, power-exempt, secondary lodging after hotels. |
| `beach_hut` | Places beach loungers on completion. |
| `main_pool` | Places the pool ball on completion. |
| `ice_stall`, `fast_food` | Food stand whose stock is not spoiled by water contamination. |
| `bar` | Counted as a bar by world scripts. |
| `cocktail_bar` | Bar that uses the beer's cocktail name. |
| `souvenir_shop` | Souvenir commodity provider. |
| `general_store` | General-goods shop tick. |
| `toilet` | Cleaning counter, flies/smoke when dirty, cleaner feedback. |
| `first_aid` | Guest first-aid destination. |
| `power_generator` | Generates power for the resort grid and power events. |
| `water_cleaner` | Reduces water pollution while running. |
| `equipment_hire` | Starts with 20 snorkels, lilos, and surfboards. |
| `jetski_hire`, `speedboat_hire` | Watercraft hire with engine idle sound. |
| `dj_club_footprint`, `dj_club_offset` | Nightclub with a DJ positioned by footprint or per-rotation offset. |
| `stage` | Creates stage show events at startup. |
| `jetty` | Built immediately without builders; excluded from rep/guard patrols. |
| `lifeguard_post`, `mechanic_home`, `builder_home`, `cleaner_home`, `rep_office`, `security_post` | Staff home buildings and browser buttons (`builder_home` and `mechanic_home` are power-exempt; `lifeguard_post` owns a swimming area). |
| `ship_stop`, `bus_stop`, `complaints_hut` | Arrival/boarding stops and the power-exempt complaints hut. |

## Sprites and icons

Run `tools/make_sprite.py` to convert PNG frames into an indexed `obl.sprite` v1 asset:

- `--kind building` writes a 256-color palette (`<name>.palettes.png`), a `color` plane sheet, and a matching `depth` plane sheet (auto-generated vertical gradient if `--depth` is omitted) plus `depth_extents`. Optional `--lightmap`, `--shadow`, and `--pick` PNGs add the corresponding building planes (`lightmap` glows at night when `[Video] LightEffects=1`).
- `--kind ui` writes a 256-color UI sprite with default hotspot `[0, 0]`, suitable for `icon_sprite` (48x48 on L0), `icon_locked_sprite`, and `preview_sprite`.
- `--directions N` splits frames across `N` rotations; pass `--repeat-directions` to reuse one set of frames across all directions via `frame_map`.
- `--spec <file.json>` builds multi-animation sprites from a JSON description.
- All frames in an animation must share the same pixel dimensions; mismatched sizes are refused with an error.
