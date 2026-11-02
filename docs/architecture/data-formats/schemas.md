# Data formats: JSON Schemas and their validator

Every JSON format of this directory has a JSON Schema in `data/schemas/` (in git, our own definitions, no content of the
source game). The schemas are the reference for field names, types and which fields are required; the format pages explain
meaning and conversion. Every JSON format has a schema and a hand-made sample; the GUI converter and portable GUI reader validate their documents with them.

## Files
| Path | Content |
|---|---|
| `data/schemas/<kind>.schema.json` | the schema of format `obl.<kind>`; its `$id` is the format id (`obl.building`) |
| `data/schemas/common.schema.json` | `obl.common`: shared definitions (`key`, `text_key`, `path`, `vec2`, `size2`, `range`, `direction`, ...), not a file format |
| `tests/schema_samples/<kind>.json`, `<kind>.<variant>.json` | hand-made samples, at least one per format; invented values, never copied from the game |
| `src/core/json_schema.{h,cpp}` | `obl::json_schema::Registry`, the validator |
| `tests/unit/json_schema_test.cpp` | the `schema.*` unit cases |

Formats with a schema (the page that explains each):

| Schema | File | Page |
|---|---|---|
| `obl.pack` | `pack.json` | [principles.md](principles.md#packs) |
| `obl.sprite` | `sprites/<name>.json` (also path-tile `autotile`) | [assets.md](assets.md#sprites), [game-data.md](game-data.md#path-tiles) |
| `obl.palette_modifiers` | `sprites/palette_modifiers.json` | [assets.md](assets.md#sprites) |
| `obl.image` | `images/cursor.json` | [assets.md](assets.md#other-images) |
| `obl.font` | `fonts/<name>.json` | [assets.md](assets.md#fonts) |
| `obl.text` | `text/<lang>.json` | [assets.md](assets.md#texts) |
| `obl.sound_script` | `sounds/sound_script.json` | [assets.md](assets.md#sounds) |
| `obl.water_anim` | `maps/water/<k>.json` | [maps-scenarios.md](maps-scenarios.md#water-shimmer) |
| `obl.game_parameters` | `rules/game_parameters.json` | [game-data.md](game-data.md#game-parameters-and-rule-constants) |
| `obl.weather_parameters` | `rules/weather_parameters.json` (v2 only) | [game-data.md](game-data.md#weather-lighting-parameters) |
| `obl.catalog_index` | `catalog/index.json` | [game-data.md](game-data.md#catalog) |
| `obl.building`, `obl.scenery`, `obl.staff`, `obl.vehicle`, `obl.table`, `obl.event`, `obl.beer`, `obl.fine`, `obl.commodity_family`, `obl.commodity`, `obl.opening_hours`, `obl.desire`, `obl.desire_slot` | `catalog/<kind>/<key>.json` | [game-data.md](game-data.md#catalog) |
| `obl.guest_set` | `guests/<set>.json` | [game-data.md](game-data.md#guest-sets) |
| `obl.object_browser`, `obl.credits` | `gui/object_browser.json`, `gui/credits.json` | [converter/index.md](converter/gui-runtime.md#gui-writer-and-runtime-handoff) |
| `obl.gui_layouts` | `gui/layouts.json` | [game-data.md](game-data.md#gui-layouts) |
| `obl.map` | `maps/<map>/map.json` | [maps-scenarios.md](maps-scenarios.md#maps) |
| `obl.map_props` | `maps/props.json` | [converter/maps.md](converter/maps.md#prop-keys) |
| `obl.scenario` | `scenarios/<id>/scenario.json` | [maps-scenarios.md](maps-scenarios.md#scenarios) |
| `obl.campaign` | `scenarios/campaign.json` | [maps-scenarios.md](maps-scenarios.md#campaign) |

`obl.save` has its schema and a hand-made sample ([saves.md](saves.md)). Script and GUI layout files are text,
not JSON; their first line (`#obl-script 1 object`, `#obl-gui 1`) is checked by their readers.

A file says which schema it follows by its `"format"`; the validator looks the schema up by that `$id`. Cross-file
references use the id: `"$ref": "obl.common#/$defs/key"`.

## Rules for a format schema
The `schema.data_schemas_are_consistent` case enforces the first four:
- File `<kind>.schema.json`, `"$id": "obl.<kind>"`, top-level `"type": "object"` with `"additionalProperties": false`.
- `properties.format` is `{"const": "obl.<kind>"}`, `properties.version` a `const`; `required` starts with
  `"format", "version"`.
- At least one sample in `tests/schema_samples/` (`schema.samples_validate` also checks that an unknown top-level key
  and a missing `version` are refused for every sample).
- Objects are closed (`additionalProperties: false`) at every level unless they are maps keyed by content keys.
- Numbers that the source stores as `float` are `"number"`; the bit-exact rule of [principles.md](principles.md#conventions)
  is a writer/reader rule, not a schema rule. Counts, ids and pixel coordinates are `"integer"`.
- Enums are lowercase strings of our own names. Source keywords stay in the converter (provenance rule).
- A new format version changes the `version` const. If a reader must accept two versions, the old schema stays as
  `<kind>.v<n>.schema.json` with `$id` `obl.<kind>.v<n>` (decided when the first version 2 appears).

## The validator
`Registry::Add` takes a schema document, `CheckRefs` lists unresolved `$ref`s, `Validate(id, instance)` returns errors
as `"<JSON pointer>: <message>"`. It implements this subset of draft 2020-12 and **refuses any other keyword when the
schema is added**, so a schema cannot rely on a keyword that is silently ignored:

| Group | Keywords |
|---|---|
| annotations (ignored) | `$schema`, `$id`, `$comment`, `title`, `description`, `examples`, `default` |
| structure | `$defs`, `$ref` (`#/pointer` or `<id>#/pointer`; applied together with sibling keywords, as in 2020-12) |
| any value | `type` (string or list; `integer` accepts `2.0`), `enum`, `const` |
| objects | `properties`, `patternProperties`, `additionalProperties`, `propertyNames`, `required`, `minProperties`, `maxProperties` |
| arrays | `prefixItems`, `items` (after the prefix), `minItems`, `maxItems`, `uniqueItems` |
| numbers | `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum` |
| strings | `minLength`, `maxLength` (code points), `pattern` (`std::regex` ECMAScript, unanchored search) |
| combinators | `allOf`, `anyOf`, `oneOf` |

Not implemented: `if`/`then`/`else`, `not`, `dependentRequired`, `contains`, `multipleOf`, `format`, `$anchor`,
`$dynamicRef`, remote URLs. Add a keyword with a unit case when a schema needs it.

The schemas stay valid draft 2020-12, so standard tools agree with our validator: a local cross-check with Python
`jsonschema` (`Draft202012Validator` plus a `referencing.Registry` of the files by `$id`) finds the same results on the
samples. It is not part of CI (no new Python dependency there).

## Where validation runs
- The unit cases on the samples (`obl_unit_tests schema.`; Linux CI runs them through `tools/unit_linux.sh`).
- The game's loaders check what they read themselves (types and
  references with file names in the error) and need not run the validator at load.
