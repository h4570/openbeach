# Mods

A mod is a directory `<game dir>/mods/<id>/` with a `mod.json` manifest. Next to it sit the mod's files under their logical
paths (`catalog/...`, `text/...`, `sprites/...`), the same paths a pack uses. A file in a mod replaces the whole file of the
same path below it. Text tables, the catalog's record list, object script vocabularies and GUI layouts merge instead (see "Text patches", "Catalog patches",
[object-scripts.md](object-scripts.md) and [gui-patches.md](gui-patches.md)); every other JSON data file takes a [JSON data patch](data-patches.md).

## mod.json
```json
{
  "format": "obl.mod",
  "version": 1,
  "id": "my_mod",
  "title": "My mod",
  "mod_version": "1.0.0",
  "pack": "base",
  "requires": ["other_mod"],
  "scope": "world",
  "api": 1,
  "settings": [{ "id": "wide", "type": "toggle", "label": "Wide layout" }]
}
```
- `id`: the directory name; lowercase letters, digits, `_` and `-` (the rules of a pack id, `pack_path::PackDirectory`).
- `pack`: the id of the pack the mod was made for (`base`, `l0`); the mod only starts on that pack. A mod that does not depend
  on content lists several instead, `"packs": ["base", "l0"]`, and starts on any of them; a manifest names `pack` or `packs`,
  not both.
- `requires` (optional): mod ids that must be active and earlier in the load order.
- `scope` (optional): `"world"` (the default) or `"presentation"`. A presentation mod reaches the simulation only through game
  commands, so it ships no data the simulation reads, and saves and replays leave it out ([scripting.md](scripting.md#scope)).
- `api` (optional): the presentation script API version the mod's scripts were written for; a mod with
  `scripts/presentation/*.lua` must name it ([scripting.md](scripting.md)).
- `settings` (optional): what the player can set for the mod, see [Settings](#settings); presentation mods only.
- `actions`, `bindings` (optional): named actions the mod adds and keys it moves, see [actions.md](actions.md).

## Settings
A mod lists the choices it offers the player in `settings`, at most 32. Every entry has an `id` (lowercase letters, digits, `_`
and `-`, unique in the mod), a `type`, a `label` (1 to 80 characters) and an optional `hint`; the labels and hints are what
the mods screen shows, in the mod's own language.

| `type` | More fields | Value |
|---|---|---|
| `"toggle"` | `default` (`true`/`false`, default `false`) | a boolean |
| `"choice"` | `options`: 1 to 32 texts, or `{ "value": "sea", "label": "Deep sea" }` objects; `default` an option's value (default the first) | the option's `value`, a string |
| `"number"` | `min`, `max`, `step` (`min < max`, `0 < step <= max - min`); `default` within the range (default `min`) | a number: an integer when `min`, `max`, `step` and `default` are whole |

The player's values live in `openbeach.ini`, in the section `[Mod.<id>]` under the setting's `id` (`[Mod.my_mod]`
`wide=1`), apart from the mod and from saves; a toggle also reads `true`/`false`, `yes`/`no`, `on`/`off`. A value that is
missing, or that the setting does not take (an unknown option, a number outside the range), is the default; the game logs
the second case once. A mod with a malformed `settings` stops the game at start-up like any other bad manifest, naming the
mod, the setting and the reason.

Only a presentation mod (`"scope": "presentation"`) may declare settings: nothing in a world mod can read them, and world scripts
must never see them, so the start-up check refuses a world mod with a `settings` list, naming the mod.

Scripts read the values with `obl.settings.get(id)` and hear a change in `on_setting(id, value)`
([scripting.md](scripting.md#settings)). Settings are for how a mod looks and reacts: **no setting changes the simulation**.
A mod that changes the world is a world mod, recorded in saves and replays, and the values of its settings are not.
Changing the engine's own settings does not move a world hash either ([settings.md](../../build/settings.md#rule)).
Code: `src/core/settings_schema.h` (the description the settings window uses for the engine's rows too), `mod_pack.cpp`.

## Activation
The active mods and their order come from the `Active` key of the `[Mods]` section in `openbeach.ini`, comma separated
(`Active=first,second`); the default is no mod, and a later mod shadows an earlier one. `OBL_MODS` (same format) replaces
it for tests and tools, also when it is empty. For players, see [switching-mods.md](../players/switching-mods.md).

## Layers
`pack_path` resolves a logical path through the first existing regular file of: the active mods from last to first,
the legacy `<game dir>/mod/` directory, then the pack. `ListStems`, sprite enumeration and the sound bank merge all layers
and list a name once. The legacy `mod/` directory keeps working unchanged.

## Start-up check
`mod_pack::Activate` (`src/formats/mod_pack.cpp`), called from `InitGame` after the pack check, stops the game with the mod id
and the reason when an active mod has no directory or no valid manifest, names another id than its directory, does not list the
selected pack, is listed twice, needs a mod that is not active or comes later, is a presentation mod that ships a file its scope
does not allow, needs a newer script API than the game has, or ships scripts without naming `api`. After the world script
vocabulary loads, `world::CheckModWorldScripts` parses every `scripts/world/*.wsc` and stops the game on an error or a
refused call ([world-scripts.md](world-scripts.md#refused-calls)). It also computes each mod's content hash once
(`pack_path::ModContentHash`: SHA-256 over the sorted relative file names and the hash of every file).

## Text patches
A mod may ship `text/<lang>.patch.json` instead of a whole table:
```json
{ "format": "obl.text_patch", "version": 1, "entries": { "KEY": "new text", "NEW_KEY": "added text" } }
```
- The loader reads the base table (the pack's, or a full replacement from a layer), then applies the patch of each active
  mod in load order; a later mod wins on the same key. Keys match ignoring case, as lookups do.
- A key the base lacks is added. Removing a key is not supported. A presentation mod's patch may only add keys.
- A mod may patch a language the pack does not have: the patch applies on top of the pack's own language (else the first
  table in settings order), so untranslated keys still show text, and the language counts as available at start-up.
- A malformed patch stops the loading with an error naming the mod, the file and the key: invalid JSON, an unknown format
  or version, an `entries` that is not an object, a value that is not a string, a key that is not ASCII.

## Catalog patches
`catalog/index.json` lists the records of each category, and that order gives the records their indices. A mod adds records
without copying it, with `catalog/index.patch.json`:
```json
{ "format": "obl.catalog_index_patch", "version": 1, "add": { "buildings": ["my_kiosk"], "commodities": ["my_soda"] } }
```
- The record files sit at the usual `catalog/<category>/<name>.json` of the mod. The loader reads the pack's `index.json`
  (or a full replacement from a layer), then appends the `add` names of each active mod in load order: existing records
  keep their indices, mod records get the next ones.
- References between records resolve by name across the merged list, so a mod building may use a mod commodity.
- Changing an existing record is a whole-file shadow of its `catalog/<category>/<name>.json`.
- A refused patch stops the loading with an error naming the mod and the record: a name that already exists (in the pack,
  an earlier mod or the same patch), a missing record file, a record whose `key` differs from its name, more records than the
  engine's limit for the category (`WorldParams::kMaxBuildings`, ...), or a malformed patch (format, version, `add`).
- Removing, reordering or renaming records is not supported. To make a mod building buildable with its own icon and sprites, see [buildings.md](buildings.md) and [first-building.md](../guides/first-building.md).

## Saves and replays
A save records the active world mods in `header.mods` and a replay in its `mods` line (id and content hash, load order), only when
one is active; presentation mods are left out. Loading a save with other mods asks the player and names the mod; a replay with other mods is refused. Details:
[saves](../../architecture/data-formats/saves.md#mods) and [replay file](../../architecture/commands.md#replay-file).

## Tests
`settings_schema_test` (the description of a setting, and that the engine's settings agree with it) and `mod_pack_test` (the
`settings` of a manifest and what is refused); `tests/l0_mod_settings.txt` runs the test mod `tests/mods/l0_settings`, which reads
its settings and hears each change (`mod-setting <mod> <id> <value>` does what the mods screen will do).
`pack_path_test` and `mod_pack_test` (unit); `tests/l0_mod.txt` runs the L0 pack with the test mod `tests/mods/l0_test`, which
`tools/runtests.ps1` copies to `run/mods/`. `text_load_test` covers the text patches; `tests/l0_mod_text_patch.txt` runs the mod
`tests/mods/l0_patch`. `catalog_patch_test` covers the catalog patches; `tests/l0_mod_catalog_patch.txt` builds the
building that the mod `tests/mods/l0_catalog` adds, and `tests/l0_mod_building.txt` exercises the complete worked example
`tests/mods/l0_ice_stand` (build panel icon, placement, construction, day/night goldens, guest purchases, sound, save/reload). `convert_save_checks_test` and `command_recorder_test` cover the mod lists of
saves and replays; `tests/l0_mod_save.txt` saves with the test mod, reloads with it, and is refused with another mod list (`claim-mods`,
`expect-obl-save-refused`). The presentation scripts' tests are in [scripting.md](scripting.md#tests); `tests/l0_mods_mixed.txt`
runs a world mod and a presentation mod at once and checks that the save records only the world mod. The world scripts' tests
are in [world-scripts.md](world-scripts.md#tests).
