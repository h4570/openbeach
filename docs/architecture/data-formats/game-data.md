# Data formats: rules, catalog, guests, scripts, GUI

Pack formats for game rules, catalogs, guests, scripts and GUI data. Everything on this page is tier L1;
shared rules are in [principles.md](principles.md).

## Game parameters and rule constants
- `Data/Script/GameParameters.pis` -> `rules/game_parameters.json`: `"parameters"`, a flat object of named values with
  our names (`snake_case`; boolean, integer, float or string as the source entry's type); the converter maps the
  source keys. Numbers follow the float rule of [principles.md](principles.md#conventions).
  Converter status: `obl-convert game-parameters` writes every entry of the installed file (no key is dropped, so a key
  the engine does not read yet is kept too). It reads the file with the `.pis` reader (`importer/src/readers/pis.h`) and applies
  GameParameters_Construct and GameParameters_Find: a lowercase type letter (an upper-case one is a fatal error in the
  engine), `b` true only for "true", `i` truncated like `FloatToInt`, `f` the reader's float, and a repeated exact key
  keeps its first value (lookup is case-sensitive, unlike the `.pis` lookup). The name is the key without its letter,
  lowercased (`bPLAY_MOVIE` -> `play_movie`). Refused, because a node slot is 0x100 bytes: a key over 255 bytes, a string over
  255 bytes, and a non-finite float (JSON has no infinity). Known limit: the engine keeps `bFOO` and `iFOO` (or `bFOO` and
  `bfoo`) as two nodes, but the JSON name drops the letter and the case, so the converter refuses both (no installed file has
  one). The check against the engine's list: `data.convert_game_parameters_match_engine` builds the list with the common
  construct (`game/game/game_parameters_construct.cpp`) and compares every
  node with the JSON (bool, int, float bits, string). ([converter/index.md](converter/index.md)).
  Game status: the game reads only this file (`packs/base/rules/game_parameters.json`,
  [../../build/game-data.md](../../build/game-data.md#converted-pack-in-runpacksbase)). `game_parameters_construct::FromJson`
  rebuilds the engine's key from the JSON type and the upper-cased name (`true` -> `b`, integer -> `i`, a number with a
  fraction or exponent -> `f`, string -> `s`; the converter writes floats such as `60.0` with their point), so the list
  equals the one from the `.pis` in order, names and values (`data.game_parameters_json_list_matches_engine`). Limit: a
  repeated key gives the engine a second, unreachable node; the JSON keeps only the first, so that list would be shorter
  (no installed file repeats a key). Source key names must be upper case after the letter (all installed ones are).
- Game rules (thresholds, rates, the special objects' script keys) are not content files: they are named constants of
  our code, since own scenarios run on the same mechanics. Tables that only map source object names to behavior are
  content (`behavior`, below).

## Catalog
`BuildingList.ble` (one file with all object definitions) becomes one directory per kind and one file per entry, so a
mod or an own pack can replace a single building:
```
catalog/index.json                      order of every kind (ids)
catalog/commodity_families/<key>.json   catalog/commodities/<key>.json   catalog/opening_hours/<key>.json
catalog/desires/<key>.json              catalog/staff/<key>.json         catalog/events/<key>.json
catalog/beers/<key>.json                catalog/fines/<key>.json         catalog/vehicles/<key>.json
catalog/tables/<key>.json               catalog/scenery/<key>.json       catalog/buildings/<key>.json
catalog/desire_slots/<key>.json
```
- **Ids are positions in `index.json`.** The source assigns ids in file order, saves and world hashes contain them
  (`m_ValidBuildingIDs`, object type ids). Saves reference keys ([saves.md](saves.md)).
- **References** between entries use keys (`"family": "snacks"`, `"opening_hours": "allhours"`); the loader resolves
  them to ids. A missing key is a load error naming the file, not a crash at use.
- **Names** shown to the player are text keys (`"name": "BUILD_BEACHBAR_NAME"`-style keys of the converted text table).

A building (fields grouped; every source `BUILD_` field has a home, the converter refuses unknown ones):
```json
{
  "format": "obl.building", "version": 2,
  "key": "beachbar", "name": "<text key>", "category": "food", "behavior": "bar",
  "sprite": "beachbar", "icon": 12, "menu": "<gui dialog>", "requires": "<prerequisite>",
  "cost": { "build": 1500, "refund": 500 },
  "construction": { "build_time": 2.0, "demolish_time": 1.0, "max_builders": 2, "anim": "<sprite>" },
  "size": [3, 2], "build_on": ["land", "beach"], "baseplates": [1],
  "points": { "serve": [1, 2], "staff": [0, 1], "entrance": [1, 3], "walk": [[0, 0], [2, 0]] },
  "operation": { "guest_capacity": 8, "opening_hours": "allhours", "max_instances": 0, "max_queue": 6,
                 "attractor": "alcohol", "power": 5, "staff": { "types": ["barman"], "max": 2 } },
  "commodities": [{ "commodity": "beer", "intensity": [0.5, 0.5], "daily_intensity_factor": 1,
                    "price": 2.5, "daily_price_factor": 1, "cost": 0.85 }],
  "failure": { "average_time": 30, "percent": 10, "age": 5 },
  "vehicles": ["pedalo"], "vehicle_places": [{ "dir": 0, "offset": [2, 0] }],
  "tables": [{ "table": "bar_table", "pos": [1, 1] }],
  "events": ["singing"],
  "anims": [{ "sprite": "...", "points": [[0, 0], [0, 0], [0, 0], [0, 0]], "flag": true }],
  "staff_files": { "female": "...", "male": "..." },
  "footprint": { "size": [3, 2], "layers": { "placement": [[...]], "work": [[...]], "depth": [[...]], "body": [[...]] } }
}
```
- `commodities` names the six numbers of the source `COMMODITY` line (intensity for men and women, their daily
  factor, start price, daily price factor, cost per serving). For alcohol the price and cost come from the beer, as now.
- `behavior` replaces the engine's dispatch on source building names (stage shows, the jetty, the sea, security
  posts); the converter sets it from the source key, the catalog loader stores it in `BuildingDef::m_Behavior`
  (the padding before `m_ServeOffset`, so the record keeps its size), and the engine reads it from the building's
  definition. Values are our enum, [building_behavior.h](../../../src/game/tables/building_behavior.h), each with the
  engine sites that use it. The behavior query `object_manager::FindBuildings(manager, behavior, count)`
  ([object_manager_queries.h](../../../src/game/objects/object_manager_queries.h)) walks the buildings in list order and
  replaces the name lookups of HOTEL, ACCOMBLK, WHARF, BUSSTOP, COMPLAINTHUT, BAYWATCH and the bar count (BEACHBAR,
  POOLBAR, BAR1, BAR2 are Bar, COCKTAIL is CocktailBar). The environment contracts of the same sites (`FindBuildings`,
  `CountBuildings`) use it too, and so do the DJ decks of `staff_service_home.h` (CLUB1, CLUB2 by `Behavior(home)`); script keys,
  `bin`, and the `ObjectExists`/`WHARF` GUI check still stay by name.
- **Footprints** are read from the converted catalog. They are a field of the
  building: four layers (`placement` mask, `work` flags and builders' work points, `depth` offsets, `body` and
  surface), each stored as the (h+2) rows of (w+2) cells the engine reads; the converter verifies that the cells it
  drops are unused.
  Values stay numeric masks with our documented bit names. Buildings without a footprint (the sea, courts, jetties)
  omit the field.
- **Effective values for source parser bugs.** The source stores a building's `EVENT` under the global event count
  instead of the building's own count (a stage with `WETTSHIRT` ends up with three `SINGING` slots). The converter
  writes what the current game ends up with (`"events": ["singing", "singing", "singing"]`); a later fix is a data
  change with its own test.
- `vehicles` and `vehicle_places` are separate lists because the source counts vehicles and their start places
  apart; `baseplates` lists the type of each plate; `staff.types` lists every allowed staff type.
- Scenery, staff, vehicles, tables, events, beers and fines follow the same pattern; the field lists are in their
  schemas (`data/schemas/<kind>.schema.json`, [schemas.md](schemas.md)). A table has one `chairs` entry per chair
  (position, direction, walk point indices, waiter serve point); the converter refuses a record whose chair counts
  disagree. Beer `effect` is `fight`, `flirt`, `vandalise`, `happy` or `sad`.

## Guest sets
`Guests01..12.ble` and `Sandbox{E,M,H}_Guests.ble` -> `guests/<set>.json` (`campaign_01`, `sandbox_easy`, ...):
```json
{
  "format": "obl.guest_set", "version": 1,
  "types": [{ "key": "lad", "name": "<text key>", "gender": "m", "sprite": "fit_bloke", "default_sprite": 0,
              "colour": 1, "money": 120, "nights": [1, 4],
              "preferences": { "alcohol": [0.4, 0.9], "snacks": [0.2, 0.6] },
              "tolerances": { "beauty": [0.2, 0.8], "crowding": [0.3, 0.9] },
              "start": { "bladder": 0.1, "drunk": 0 }, "decay": { "drunk": 0.01 },
              "sleep_time": [22, 24], "wake_time": [7, 9], "max": { "alcohol": 1 } }],
  "names": { "surnames": ["<text key>"], "male": ["<text key>"], "female": ["<text key>"] }
}
```
Field groups mirror the source record with our names (`preferences` for 13 activities, `tolerances`,
`opinion_effects` and `opinion_start` for seven opinions, `start` values, `decay` rates, `max` limits for 26 actions);
the field list is in `data/schemas/guest_set.schema.json`. A member missing from a group is 0, so the converter may
write only non-zero values. Name pools stay text
keys (a key may hold spaces: the source has `ID_DEL BOSCO`). The engine ignores the source's `NAMESET`, so the
document has no such field, and every source guest has an empty `NAME`. Keys and sprite names are lowercase; the
reading of the source files is [converter/guests.md](converter/guests.md). Set version 2 keeps only those canonical names; the engine's
lookups ignore case. The
engine loads `packs/base/guests/<set>.json` for the set a scenario's `WORLD_GUESTLIST` names (the file name maps through
`src/game/objects/guest_sets.h`); the `.ble` files are not read. A scenario names its guest set ([maps-scenarios.md](maps-scenarios.md#scenarios)).

## Scripts
Object scripts (`Data/Script/Object/*.ble`: guest, staff, shark behavior) and world scripts (each scenario's logic)
are content: they are the source game's AI and scenario logic.
- Plain UTF-8 text in the **existing syntax**, decoded (no XOR), with a first line `#obl-script 1 object` or
  `#obl-script 1 world`: `scripts/object/<name>.obs`, `scenarios/<id>/world.wsc`.
  The converter writes these files: [converter/scripts.md](converter/scripts.md).
- The **vocabulary**: the functions and values are the engine's own script API (it declares the names it implements,
  [converter/scripts.md](converter/scripts.md#vocabulary)); the state and message names are read from the executable's
  tables into `scripts/*_vocabulary.json`. Own scripts may use the same syntax and keywords.

## GUI layouts
`Data/Script/GUI/*.ble` (main menu per language, in-game GUI, Polish variants) -> `gui/<name>.gui`, decoded UTF-8 text
in the existing syntax with a `#obl-gui 1` first line, same as scripts. Per-language files are separate; the
language-to-file choice is in `gui/layouts.json` (`"layouts": {"main_menu": {"pl": "main_menu_pl.gui",
"*": ...}}`, `"*"` required as the fallback), and the engine reads it instead of the installed files. The font choice per
language (`"fonts": {"pl": {<font role>: <font key>}}`) is not written yet. GUI layouts are L1 because the headless host builds the GUI (bots read its dialogs).
The converter writes these files: [converter/gui-layouts.md](converter/gui-layouts.md).

GUI tables compiled into the executable:
- **Object browser:** the filter *kinds* (which object variable, sort mode, bar kind) are our code with our names
  (`guest_happiness`); the *presentation* (the guest, building, staff, hire, event and cell-type lists with order and
  separators, text keys, column headings, row button sprites and tooltips) is converted content in
  `gui/object_browser.json`, whose entries name a filter kind.
- **Credits:** the credits screen layout (font, text line, color, spacing per row) is converted content
  (`gui/credits.json`); OpenBeach's own credits (`src/game/game/own_credits.cpp`) are merged with it.
- **GUI actions** (the names layouts bind buttons to), **screen resolutions** and small constants (`"None"`, `"NONE"`,
  the `ButtonClick` sound, `%d`) are our code.

## Path tiles
`pathtiles.ble` (256 neighbor masks to a frame of the `pathtile*` sprites) is tileset metadata: it becomes an
`"autotile"` table in the path tileset's sprite JSON (`"autotile": {"masks": [-1, 12, ...], "full": 67}`, 256 entries
in mask order N, NE, E, SE, S, SW, W, NW = bits 0..7). The converter writes the effective table (the source reader
keeps only the low byte and the last duplicate).

The converter writes the table into every `pathtile_*` sprite of the installation or CD. The engine uses the selected
pavement sprite's `autotile.masks` and `autotile.full`, including for background restoration, and never opens the `.ble`.
Missing entries retain zero; low byte 255 is written as -1. The archive oracle checks the converted table against the
previous parser. The headless metadata replay fixture also omits `pathtiles.ble`.

## Weather lighting parameters

`rules/weather_parameters.json` (`obl.weather_parameters`, version 2 only) supplies the
weather constructor with `light_keys` and `state_colors`. The schema requires exactly
24 `[hour, rgb]` rows and 14 RGB integers (0 through 16777215). The reader additionally
requires hours 0 through 23 in order and refuses missing rows before publishing any
parameters. There are no compiled palette defaults. Interpolation, state blending,
lighting updates and the save format stay in the engine.

The game-parameters importer command also writes this validated document, preserving
the imported lighting palette. `tools/gen_l0_pack.py` authors L0 independently: a white
day curve, zero state darkening and white flash colors. No other L0 content changes.
