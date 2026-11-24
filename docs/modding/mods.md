# Mods

A mod is a directory `<game dir>/mods/<id>/` with a `mod.json` manifest. Next to it sit the mod's files under their logical
paths (`catalog/...`, `text/...`, `sprites/...`), the same paths a pack uses. A file in a mod replaces the whole file of the
same path below it. Text tables merge instead (see "Text patches"); merging catalog records is not done yet.

## mod.json
```json
{
  "format": "obl.mod",
  "version": 1,
  "id": "my_mod",
  "title": "My mod",
  "mod_version": "1.0.0",
  "pack": "base",
  "requires": ["other_mod"]
}
```
- `id`: the directory name; lowercase letters, digits, `_` and `-` (the rules of a pack id, `pack_path::PackDirectory`).
- `pack`: the id of the pack the mod was made for (`base`, `l0`); the mod only starts on that pack.
- `requires` (optional): mod ids that must be active and earlier in the load order.

## Switching mods on
The active mods and their order come from the `Active` key of the `[Mods]` section in `openbeach.ini`, comma separated
(`Active=first,second`). `OBL_MODS` (same format) replaces it for tests and tools, also when it is empty. The default is no
mod. A later mod shadows an earlier one.

## Layers
`pack_path` resolves a logical path through the first existing regular file of: the active mods from last to first,
the legacy `<game dir>/mod/` directory, then the pack. `ListStems`, sprite enumeration and the sound bank merge all layers
and list a name once. The legacy `mod/` directory keeps working unchanged.

## Start-up check
`mod_pack::Activate` (`src/formats/mod_pack.cpp`), called from `InitGame` after the pack check, stops the game with the mod id
and the reason when an active mod has no directory or no valid manifest, names another id than its directory, targets another
pack, is listed twice, or needs a mod that is not active or comes later. It also computes each mod's content hash once
(`pack_path::ModContentHash`: SHA-256 over the sorted relative file names and the hash of every file).

## Text patches
A mod may ship `text/<lang>.patch.json` instead of a whole table:
```json
{ "format": "obl.text_patch", "version": 1, "entries": { "KEY": "new text", "NEW_KEY": "added text" } }
```
- The loader reads the base table (the pack's, or a full replacement from a layer), then applies the patch of each active
  mod in load order; a later mod wins on the same key. Keys match ignoring case, as lookups do.
- A key the base lacks is added. Removing a key is not supported.
- A mod may patch a language the pack does not have: the patch applies on top of the pack's own language (else the first
  table in settings order), so untranslated keys still show text, and the language counts as available at start-up.
- A malformed patch stops the loading with an error naming the mod, the file and the key: invalid JSON, an unknown format
  or version, an `entries` that is not an object, a value that is not a string, a key that is not ASCII.

## Tests
`pack_path_test` and `mod_pack_test` (unit); `tests/l0_mod.txt` runs the L0 pack with the test mod `tests/mods/l0_test`, which
`tools/runtests.ps1` copies to `run/mods/`. `text_load_test` covers the text patches; `tests/l0_mod_text_patch.txt` runs the mod
`tests/mods/l0_patch`.
