# Mods

A mod is a directory `<game dir>/mods/<id>/` with a `mod.json` manifest. Next to it sit the mod's files under their logical
paths (`catalog/...`, `text/...`, `sprites/...`), the same paths a pack uses. A file in a mod replaces the whole file of the
same path below it; merging catalog records and text tables is not done yet.

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

## Tests
`pack_path_test` and `mod_pack_test` (unit); `tests/l0_mod.txt` runs the L0 pack with the test mod `tests/mods/l0_test`, which
`tools/runtests.ps1` copies to `run/mods/`.
