# Data formats

The formats the game reads, and the converter that produces them from the user's own copy of Beach Life (`importer/`,
[converter/index.md](converter/index.md)).

## In one paragraph
The game and `obl-sim` read only packs of our own formats (JSON with a format id and version, PNG, WAV, MP3, MPG, and
script text). `obl-convert` builds the `base` pack from the user's copy, deterministically and losslessly (world hashes
and golden images stay equal); own packs (such as the L0 scenario) use the same formats and live in git; mods
override files by path. Knowledge of the source's file formats lives only in the converter, content of the source
only in converted packs on the user's machine. Files are tiered: L0 own content, L1 simulation data (JSON,
scripts, terrain), L2 media (pixels, sound, music, video), so the headless simulation and cloud CI need only L1.

## Topics
| Topic | Summary |
|---|---|
| [principles.md](principles.md) | The pipeline, the provenance rule (our schemas vs. converter-only format knowledge vs. converted content), packs and `pack.json`, the L0/L1/L2 tiers and how CI gets L1, and the conventions every format follows (versioned JSON, schemas, lowercase keys, bit-exact floats, logical coordinates, PNG rules). |
| [assets.md](assets.md) | Sprites (JSON plus indexed or RGBA PNG sheet pages per animation, palettes, building planes, tone groups), screens and cursor, fonts keyed by Unicode with effective kerning, text tables in UTF-8 with the effective entry order, sound samples as WAV copies, the sound script as JSON, music and video as byte copies. |
| [game-data.md](game-data.md) | Game parameters, weather lighting parameters and rule constants, the catalog (one JSON per building, scenery object, staff type, vehicle, table, event, beer, fine, commodity, desire) with ids by `index.json` order and footprints inside the building, guest sets, object and world scripts and GUI layouts as decoded text, path tiles as an autotile table. |
| [maps-scenarios.md](maps-scenarios.md) | Maps separate from scenarios (terrain and height PNGs in L1, color and depth PNGs in L2), the water shimmer, `scenario.json` replacing `.pis`, the campaign list. |
| [l0-scenario.md](l0-scenario.md) | The minimal own L0 pack, its generated content and readable pixel fonts, loader requirements, and tests that run without source data. |
| [l0-provenance.md](l0-provenance.md) | Origin of every L0 pack member: generated (byte-compared by a tool test) or hand-authored, and the members whose independence is not yet shown. |
| [schemas.md](schemas.md) | The JSON Schemas of these formats in `data/schemas/` (one per format, `$id` = format id, shared definitions in `obl.common`), the hand-made samples in `tests/schema_samples/`, the rules every format schema follows, and our validator's draft 2020-12 subset, which refuses unknown keywords. |
| [saves.md](saves.md) | The new save format: one JSON document with the header first for the slot list, bit-exact floats, fields shared with the world snapshot, content by key, and the schema's initial scope. It points to the topics below. |
| [saves-dropped-state.md](saves-dropped-state.md) | The state the old save writer drops (guest, staff, construction, beer and queue fields) and the optional `obl.save` fields that keep it, with the chaos round trips that showed each loss. |
| [saves-records.md](saves-records.md) | Named states (the value names both the game and the converter use), named and plain records and lists in the file, and the defaults of missing fields (`FillSaveDefaults`). |
| [saves-old-reader.md](saves-old-reader.md) | Reading an old `save<N>.dat`: the borrowed-byte stream, the version-1.6 header, the typed body tables, the catalog-free checks, and `obl-convert save-info`. |
| [saves-old-conversion.md](saves-old-conversion.md) | `obl-convert save`: the placement, uninitialized-word and traversal-flag decisions, the local corpus, the text decoding, and the known traps of the old format. |
| [save-switch.md](save-switch.md) | How the game saves and loads `obl.save`: the contracts, the save panel, the test commands and references, and how old saves reach the game at first start. |
| [save-switch-sections.md](save-switch-sections.md) | The section writers (`world`, `player`, `view`, managers, `map`, `objects` by family) and readers (value rules, manager `TableReader`, tests). |
| [save-switch-objects.md](save-switch-objects.md) | The readers of the `objects` entries: construction and ids, per-object bodies from the document, links by id, pre-queue checks. |
| [converter/](converter/index.md) | `importer/`: the portable `obl_convert` library and `obl-convert` CLI, one command per format, the reader of the data tables in the supported game executables and of the CDs' InstallShield cabinets, and lossless sprite, image and Unicode font conversion with synthetic and installation tests. |
| [converter-images.md](converter-images.md) | The installation image command: exact RGB palette modifiers with tone metadata, RGBA screens and cursor atlas, grayscale haze displacement tables. Strict TGA/TIFF decoding and PNG round-trip tests, plus installation comparisons with the engine decoders. |
| [sources.md](sources.md) | Supported digital, European CD and UK/US CD copies: executable identities, data revision stamps, differing files and InstallShield input handling. |

## Design choices
The converter may contain source identifiers (file names and keywords needed to read the user's copy), never values.
Rule constants are named constants of our code; bulk content tables stay converted data. Own scripts may use the source
syntax. Mods replace whole files. Own audio is WAV and MP3. Maps are pre-rendered images; there is no terrain renderer.
Own character art is indexed. Saves are JSON. Own packs live in `data/packs/`, schemas in `data/schemas/`, the converted
pack in `run/packs/base/`.

The converter supports the digital release and both CDs. CD executables contain the August build; the digital release
contains the October build, with four changed footprints and an extra script state. Recorded test references use the
digital executable with European data. The digital release supplies no data itself; the UK/US CD supplies an older
data revision with English texts. See [sources.md](sources.md) for supported combinations.
