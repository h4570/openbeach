# Converter: pack manifest

`obl-convert pack <installation directory> <pack directory> [--language <lang>] [--schemas <dir>]`
(`importer/src/writers/pack_manifest.h/.cpp`) writes `<pack>/pack.json` ([obl.pack](../principles.md#packs)). It runs last,
after the commands that fill the pack, and hashes what is on disk.

Converted saves name the pack by its `id`, `content_version` and the SHA-256 of `pack.json`, and decode their text in
its `language` ([save-conversion.md](save-conversion.md)).

## Contract
- Only v2 exists: the game and the converter read and write v2, and older packs are converted again.
- `id` `beachlife-2002`, `title` `Beach Life (converted)`, `provenance` `converted`, `converter` `obl-convert 1`.
- `content_version` `1.0.0` (`kConvertedContentVersion`): the source's content never changes, so it moves only when
  the converter's output for the same input changes.
- `version` 2. `import.reference`: `ImportReference`, the SHA-256 of the sorted hashes of the identifying files found by
  `IdentifySource` ([sources.md](../sources.md)); an unknown source is refused. Hashes only, so path spelling does not
  matter. Build, revision, paths and hashes are not in the pack: `obl-convert identify` is the import report.
- `language`: the default text language. `--language <lang>` names it and `text/<lang>.json` must exist; without the
  flag a valid INI language is required and its table must exist. Missing/invalid INI settings or an unimported
  requested language stop the command; fix `BeachLife.ini` or choose `--language` explicitly. The lower-level
  `BuildPackManifest` API still supports automatic selection for callers without an installation INI.
- `files`: every regular file under the pack except `pack.json` itself (its hash is the pack identity), sorted by path,
  with SHA-256 and tier. Tier by kind: `.png`, `.wav`, `.mp3`, `.mpg`, `.ogg` are 2, except `maps/**/terrain.png` and
  `maps/**/height.png`, which are 1; everything else is 1 (`PackFileTier`).
- A file whose path is not a valid pack path (lowercase `[a-z0-9_.-]`, `/` separators) stops the command with its
  name; nothing is skipped silently.
- The document is validated against `obl.pack` before it is written (2-space JSON, LF). Re-running over an unchanged
  pack gives identical bytes.

## Not yet
- No single "convert everything" command yet; `pack` is run after the per-asset commands.

## Tests
`tests/unit/convert_pack_test.cpp`: tiers, hashes, sorting, `pack.json` excluded, byte-identical rerun, validation
against the embedded bundle, the import reference, language selection and its errors, a rejected path.
`tests/tools/test_pack_language.py` exercises the CLI's INI guard and explicit override without changing the live INI
or pack; its installation-dependent checks skip when the source identity files are absent.
