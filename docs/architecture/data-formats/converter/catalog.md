# Converter: the catalog (`obl-convert catalog`)

`obl-convert catalog <installation> <output> [--schemas <dir>]` reads `Data/Script/BuildingList.ble` (bytes XOR 0xFF) and the footprint grids of `BeachLife.exe` (`FindFootprints`, [tables.md](tables.md#executable-tables)) and
writes `<output>/catalog/` in the layout of [game-data.md](../game-data.md#catalog): `index.json` plus one file per commodity
family, commodity, opening hours, desire, desire slot, staff type, event, beer, fine, vehicle, table, scenery object and building. Every
document is validated before the first file is written. On the digital release: 322 files (59 buildings, 94 scenery
objects, 21 staff types, 19 tables, ...).

## Code
| File | Content |
|---|---|
| `importer/src/readers/building_list.h/.cpp` | `ParseBuildingList`: the decoded text into plain records (`BuildingList`, source keys, ids as vector positions). `ReadBuildingList` finds and decodes the installation's file. |
| `importer/src/writers/catalog_writer.h/.cpp` | `CatalogDocuments` (records to JSON documents), `WriteCatalog` (validate all, then write), `ConvertCatalog` (both), `FloatNumber` (a `float` as the shortest decimal that reads back to the same bits). |

## Reading
The reader follows the engine's parser (`importer/src/readers/world_params_process.h`, `world_params_add.h`) token for token, so the
records hold what the game loads today:
- Quoted tokens only; text outside quotes (comments) is ignored, an odd quote count is refused.
- A keyword token splits at its first `_`, except that `JETTY_` and `SECURITY_` stay in the prefix (`JETTY_END_NAME` is
  field `NAME` of `JETTY_END`). The prefix is tested in the engine's order (record kinds, then the keys defined so far),
  so a field line belongs to the first matching record.
- Values: `atoi`, `atof` stored as `float`, the engine's keyword enums. References (`OPENINGHOURS`, `ATTRACTOR`, `STAFF`,
  `COMMODITY`, ...) must name a record defined earlier in the file.
- `ANIMSPRITE` takes ten tokens, `COMMODITY` seven, `STAFF_<key>` twelve; other field lines one.
- X/Y pairs (walk points, vehicle places, table places, chair positions, waiter serve points) store into a pending slot
  and count on Y, like the engine.
- `EVENT`: the engine writes the id at index *number of events in the table* of the building's five slots. With the five
  events of the data that index is the building's count field, so the count becomes id + 1 over slots that stay 0: the
  stage's `WETTSHIRT` (id 2) gives three `singing` slots. The reader computes this result and refuses a file whose index
  would land further on.

The reader refuses what the engine would load differently or not at all: an unknown keyword, field or enum value (the engine
ignores some, e.g. unknown scenery fields), a list past its count or capacity (heap or record overrun in the engine), a
point line before its count line, duplicate keys, chair lists whose counts disagree, and guest records or name pools
(they belong to the guest sets, not the catalog).

Kept as written: chair walk point indices are not range-checked. `CONFIG1`'s last chair names index 4 of four walk points;
the engine reads past the list there like the source.

## Writing
- Keys are the source keys lowercased (`Bin` -> `bin`); a key that is not `[a-z0-9_]` afterwards, or two keys of a kind
  that differ only in case, are refused. Sprite and animation names are lowercased the same way (the sprite converter's
  keys). References are written as the target's key.
- Texts (names, menus, `requires`, staff scripts, ...) are Windows-1252 ([code_page.h](../../../../src/core/code_page.h))
  and written as UTF-8; every byte maps to one code point, so the bytes survive.
- Every value the engine reads is written, defaults included (a building without `ATTRACTOR` gets `"attractor": "none"`,
  the family with id 0). Empty strings and empty lists are left out.
- `footprint`: the grid of the building's key, cut from four 25 x 25 layers to the (h + 2) rows of (w + 2) cells the engine reads (`CanPlace`, `ApplyFootprint`): `placement` (layer 0), `work`, `depth`, `body`. Cells are unsigned masks. Refused: a grid of an unknown building, a second grid, a grid size other than the building's, a non-zero cell outside the area. All 48 grids of the data pass.
- `behavior`: `BehaviorFromKey` ([building_behavior_keys.h](../../../../importer/src/readers/building_behavior_keys.h)) on the source key,
  compared as the engine compares it (exact and case-sensitive; `JETTY` by substring), its name written for every
  building (`"none"` included). One value per key the engine names (stages, jetties, staff homes, the toilet, bars, ...).
  The engine's `"BARBEQUE"` matches no key (`BARBECUE` is), so barbecue is `none` like today; the sea and the courts are
  named nowhere and are `none`. The game does not read the field yet.

- `desire_slots`: the 26 guest desire slots in id order, with the keys of a guest set's `max` ([guests.md](guests.md#mapping)). No source file lists them: the converter holds the table, and `script_name` is the word the world scripts use for the slot's `Weather` modifier (`intimacy` is `HardSex`). Eight slots have none.

## Engine loader
`catalog::LoadPack` (`src/game/tables/catalog_load.cpp`) reads `packs/base/catalog/index.json` and the entries it lists.
The WorldParams constructor uses it instead of the scenario header's building-list path; guest lists still use their
existing reader. All three pack producers already run `obl-convert catalog` before `pack`.

The loader embeds the checked-in schemas, validates every document, preserves index order and resolves references by
content key. It checks the current fixed table, slot, string and numeric capacities before writing them. The records own
all their data (strings, vectors, the shared footprint grid); an error leaves the destination unchanged; the game stops with the failing file and the setup command.

The world script parser takes its `Weather` modifier words from the `desire_slots` rows, matched exactly; an unknown word stays a script error. The loader refuses a list without all 26 slots.

Catalog version 2 holds canonical lowercase names only: keys, `requires`, sprite and animation names. The converter does
the mapping from source spellings, and the runtime keeps these names and compares content names without case (a save may
carry an old spelling in its `name` alias). Version 1 files are refused.

## Tests
`tests/unit/convert_catalog_test.cpp`. `convert.catalog_reader_matches_engine` parses an invented list of every kind and
quirk with both the engine's `Process` and the reader and compares every field; `convert.catalog_documents_validate`
validates its documents with the real schemas and checks selected JSON; `convert.catalog_reader_refuses` covers the
refusals; `convert.building_behavior_from_key` and `convert.building_behavior_schema_matches_enum` cover the key rule and keep the
schema enum equal to the C++ names; `convert.catalog_footprints` cuts an invented grid and refuses bad ones; `data.convert_catalog_matches_engine` does the field comparison on `run/`'s file and converts it (48 buildings with a footprint).

`catalog.load_matches_engine` also loads the invented converted pack and compares every fixed field and heap-backed
list with the source parser. `catalog.load_refuses_invalid_pack_transactionally` covers missing files, invalid JSON,
schema versions, mismatched entry keys, a leftover `engine_strings` field, unresolved references and engine ranges. The `data.` test
loads its converted pack through the same runtime loader and compares it with the user's source building list.
