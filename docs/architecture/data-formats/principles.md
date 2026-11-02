# Data formats: principles, packs and tiers

The format pages of this directory build on the rules here.

## Pipeline
```
user's copy of Beach Life --> obl-convert (on the user's machine) --> packs/base/   (converted, never edited)
                                                                     packs/<own>/   (our content, e.g. L0, in git)
                                                                     mods/<name>/   (overrides, outside git)
                                                    game and obl-sim read only packs and mods
```
- The game and `obl-sim` read **only** the formats of this directory. Parsers of the source formats (`.ble` with XOR,
  `.pis`, `BLSprite`, `blsnd`, `.biz`, the save stream) live only in the converter.
- The converter is deterministic: the same input gives byte-identical output. Re-running it replaces `packs/base/`
  wholesale; nobody edits converted files by hand (changes go into a mod or an own pack).
- Where the source data relies on a quirk of the source reader,
  the converter resolves the quirk and writes the **effective** value, so the engine needs no quirk (examples: kerning in
  [assets.md](assets.md#fonts), duplicate text keys in [assets.md](assets.md#texts)).

## Provenance rule
The project holds no code or data of the source. The formats are therefore split into three kinds of
knowledge, and each may live only in one place:

| Knowledge | Example | Where it may live |
|---|---|---|
| Our format definitions (schemas, field names, file layout) | `"footprint"` of a building, the sprite JSON | engine, converter, docs, own packs (in git) |
| How to read the source (file layouts, keywords, source identifiers needed to map them) | `BUILD_<X>_GRIDWIDTH` maps to `footprint` width | converter only (`importer/`), never the engine or a schema |
| Content of the source (numbers, texts, pixels, sounds, scripts, maps, tables in the executable) | the price of the beach bar, `gail`'s palettes | converted packs on the user's machine only, never in git |

Consequences:
1. **Our own names.** Schemas use our field names (lowercase, `snake_case`), not source keywords. Content keys inside a
   converted pack keep the source names, lowercased (`beachbar`); they exist only on the user's machine.
2. **No content names in the engine.** What depends on a source name is a field of the content (`"behavior": "stage_show"`,
   `"tone_group": 3`); the converter sets it from the source name. The engine knows behaviors, not buildings.
3. **No executable tables as engine data.** Tables of the executable are converted content (footprints, GUI tables) or our
   own code (the script vocabulary, the iso tile mask). Rule constants of the mechanics (thresholds, rates) are named
   constants of our code. Executable tables come from the player's digital release or CD; `obl-convert identify` reports the build, while
   `pack.json` retains an opaque import reference ([sources.md](sources.md)).
4. **Pack provenance is declared** (`pack.json`, below) and checked: a lint refuses any committed file that belongs to a
   pack with `"provenance": "converted"` or carries the converter's marker.
   Run `python tools/check_content.py` (also in CI `lint`): it checks Git-tracked files against ancestor `pack.json`
   manifests, including untracked manifests, and rejects standalone JSON with the converter's top-level
   `"provenance": "converted"` marker. Source code and documentation quoting the marker remain allowed.
5. Hand-written synthetic fixtures in the source formats (tiny sprite banks, `.pis` snippets for converter unit
   tests) are ours and may be committed; they must not copy source bytes.

## Packs
A pack is a directory with the same layout for converted and own content:
```
<pack>/
  pack.json                 id, version, provenance, tiers, file list
  text/<lang>.json          texts                     (assets.md)
  fonts/ sprites/ images/   graphics                  (assets.md)
  sounds/ music/ video/     audio and video           (assets.md)
  rules/ catalog/ guests/   simulation definitions    (game-data.md)
  scripts/ gui/             object scripts, GUI layouts (game-data.md)
  maps/ scenarios/          maps, scenarios, campaign (maps-scenarios.md)
```
`pack.json`:
```json
{
  "format": "obl.pack", "version": 2,
  "id": "beachlife-2002", "title": "Beach Life (converted)", "content_version": "1.0.0",
  "language": "pl", "provenance": "converted",
  "import": { "reference": "5f1c..." },
  "converter": "obl-convert 1",
  "files": { "catalog/buildings/beachbar.json": { "sha256": "...", "tier": 1 } }
}
```
- `provenance` is `converted` (made by `obl-convert` from the user's copy) or `own` (authored by us, may be in git).
- `import.reference` (converted packs) is an opaque hash that tells imports apart: equal references mean the same
  imported copy. Build detection, source paths, hashes and reports stay in the converter (`obl-convert identify`,
  [converter/pack.md](converter/pack.md)). Every combination of executable build and data revision is supported
  ([sources.md](sources.md)); recorded replays, hashes and golden images
  use the digital executable with European CD data.
- `language` is the pack's default text language; its `text/<lang>.json` is in `files`. Converted packs always name
  one (`obl-convert pack`, [converter/pack.md](converter/pack.md)); own packs without texts omit it.
- `files` lists every file with its hash and tier; the hash of `pack.json` identifies the data version (CI cache key,
  bug reports, save compatibility).
- Mount order: mods (user order) over packs (`base` first, then own packs a scenario requires). A logical path such as
  `sprites/gail.json` resolves to the first layer that has it. A file replaces the lower one whole; there are no JSON
  patches.
- Locations: own packs in git under `data/packs/<id>/`, schemas in `data/schemas/`, the converted pack in
  `run/packs/base/` during development, browser storage in the browser build.
  `data/` never holds converted files.

## Data tiers
| Tier | Content | Source | Where it runs |
|---|---|---|---|
| **L0** | engine, schemas, own packs (the minimal L0 scenario with placeholder art and sounds), synthetic converter fixtures | ours, in git | everywhere: Linux CI without secrets, forks |
| **L1** | simulation data: every JSON and script of the converted pack, map terrain and height, sprite and font *metadata* | converted from a subset of the source (no sprite pixels, no `.biz`, no audio) | headless `obl-sim`, replays, bots |
| **L2** | full assets: sprite and font pixels, map color and depth, screens, sounds, music, video | converted from the whole source | the game window, golden images; local machines |

- The tier of a file is recorded in `pack.json`. Default by kind: JSON and script text are L1, media (PNG, WAV, MP3,
  MPG) are L2. Exceptions are named in the format pages (map terrain and height PNGs are L1).
- The headless host never opens L2 files: it loads sprite and font metadata
  (frame counts, sizes, hotspots, glyph rectangles) and skips pixels. Anything the simulation reads must therefore be
  L1; a simulation read of an L2 file is a bug the L1-only CI run finds.
- A test declares the tier it needs (manifest tag). Missing tier: SKIP with a hint locally, FAIL in a CI job that
  declares the tier.
- The L1 conversion needs only a subset of the source: scripts, scenario files without `.biz`, texts, `pathtiles`,
  `BLSprite.hdr` without `.dat`, `Sounds.ble` and the executable.

## Conventions
- **Text files:** UTF-8 without BOM, LF, 2-space JSON indentation, keys in schema order. Every JSON file starts with
  `"format": "obl.<kind>"` and `"version": <n>`; the engine reads versions it knows and refuses newer ones with a clear
  error. Own packs are migrated by the engine's loader (they are not regenerated); converted packs are simply converted
  again by the new converter.
- **Schemas:** one JSON Schema per format in `data/schemas/` (in git), validated by unit tests on hand-made samples and
  the L0 pack and by the converter on its own output ([schemas.md](schemas.md)).
- **Keys:** content keys are lowercase ASCII `[a-z0-9_]`. The source compares many names case-insensitively; the
  converter lowercases once and the engine compares exactly.
- **Numbers:** the source stores most values as `float`. JSON numbers are written as the shortest decimal that
  round-trips to the same `float` bits and are parsed straight to `float` (`std::from_chars`), never through `double`,
  so a value survives conversion bit for bit. Integers stay integers; enums are strings.
- **Coordinates:** pixel coordinates of the source resolution are *logical* units. Images may carry `"scale"`
  (texture pixels per logical unit, default 1) so a mod can ship larger textures with the same hotspots and footprints.
- **Images:** PNG only, written without time or text chunks, fixed compression level. Exact formats in
  [assets.md](assets.md#images-and-pixels).
- **Paths** inside JSON are relative to the pack root and use `/`.
