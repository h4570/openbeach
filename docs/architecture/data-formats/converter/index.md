# Data formats: the converter (`importer/`)

`obl-convert` turns the user's copy of Beach Life into packs of our formats ([principles.md](../principles.md)). It is the
only code that knows the source's file layouts (provenance rule), so it lives apart from the engine in `importer/`:
pure, portable C++ without Windows APIs or game layouts, built on Windows (`build.ps1`) and in the Linux unit-test job.
It covers source identification, the executable table search, the CD cabinets, sprites, palette modifiers, images, fonts, texts, GUI layouts and object and world scripts of an installation, the catalog ([catalog.md](catalog.md)), maps ([maps.md](maps.md)), scenarios and campaign ([scenarios.md](scenarios.md)).


## Layout
The converter separates input adapters, readers and output writers:
`include/openbeach/importer/` (the public API: the byte source, the pack sink and `ImportPack`), `src/source/` (byte source adapters, installation
lookup, identity, cabinets, the PE reader and executable scan, MD5), `src/readers/` (decoders of the old input files and executable tables, the `.ble`
reference parser), `src/readers/save/` (the old save grammar and its translation to `obl.save`), `src/writers/` (the
conversion steps that write pack files, PNG and `pack.json`), `src/import.cpp` (`ImportPack`: every pack step in
the producers' order, `pack.json` last), `cli/` (`main.cpp`) and `web/` (the browser adapter, below).

| File | Content | Content |
|---|---|
| `pe_file.h/.cpp` | `PeFile`: a PE32 i386 file as bytes plus its section table; maps virtual addresses to file bytes, reads `u32` and C strings. No loader, no relocations. |
| `exe_tables.h/.cpp` | `FindFootprints`: the footprint grids by content pattern (below). |
| `gui_tables.h/.cpp` | `FindGuiTables`: the credits layout by structure (below) plus our own object browser lists, column headings and row buttons (the keys name entries of the user's text tables). |
| `exe_scan.h` | Helpers of both searches: little-endian `u32`, identifiers at an address, a unique C string. |
| `source.h/.cpp` | `IdentifySource`: the executable build by the SHA-256 of `BeachLife.exe` and the data revision by the stamp of `Data/Sprites/BLSprite.hdr` ([sources.md](../sources.md)), in an installation or on a CD root (the executable lies loose, the sprite header comes from the cabinets; the hashes are keyed by installation path either way); `FindInstalledFile` resolves installation paths ignoring ASCII case on every file system. Unknown hashes or stamps are refused. `KnownExe` and `ExeBuild` are public (`openbeach/importer/known_exe.h`) because `ImportOptions` takes a table of them. |
| `cabinet.h/.cpp` | `Cabinet`: the CDs' InstallShield 6 cabinets (below); lists the files with their groups, finds one by path, extracts it streaming with size and MD5 checks. |
| `cd_source.h/.cpp`, `spool.cpp` | `OpenCdSource`: a CD root as the installed tree it would install, large files through a `Spool` (`openbeach/importer/spool.h`; directory and memory spools) ([cabinets.md](cabinets.md#cd-as-an-installed-tree)). |
| `byte_source.h`, `byte_source.cpp` | `ByteSource`: the importer's input as named files of bytes (`List`, `Size`, ranged `Read`, `ReadAll`); `FindFile` resolves paths ignoring ASCII case (an exact match wins); `OpenDirectorySource` (desktop) and `MemorySource` (browser files, synthetic tests). |
| `md5.h/.cpp` | Portable MD5 (RFC 1321) for the cabinets' per-file checksums. |
| `sprite_bank.h/.cpp` | `ParseSpriteBank`: `BLSprite.hdr` (stamp, version, sprites with palettes and animation headers); `DecodeSpriteAnim`: one animation block of `BLSprite.dat` (frame map, frame offsets, run-length rows), frames handed out one at a time. |
| `sprite_convert.h/.cpp` | `ConvertSprite` / `ConvertSprites`: `obl.sprite` JSON, palette PNG and sheet pages ([Sprites](assets.md#sprites)). `SpriteArchive` opens the archive of an installation or a CD root (the CD's `.dat` spooled from its cabinet) once for any number of conversions. |
| `image_convert.h/.cpp` | `ConvertImages`: palette modifiers, screens, cursor and haze ([converter-images.md](../converter-images.md)). Strict TGA/TIFF readers, lossless PNGs and validated metadata. |
| `map_convert.h/.cpp`, `map_decode.cpp` | `ConvertMaps`: installed island references, terrain/height/color/depth PNGs, water sheets and save-name prop lookup ([maps.md](maps.md)); bounded source decoders checked against the engine. |
| `png_write.h/.cpp` | `EncodePng`: 8-bit gray, gray + alpha, RGB, RGBA PNGs. Each row is estimated under the five filters and the winner is written straight into the deflate input and the deflate comes from a private copy of `stb_image_write` that is replaced by a faster one; both give the bytes of `stbi_write_png_to_mem` (the pack's hashes, and so converted saves, depend on them; `image.png_bytes_match_stb` and `image.png_filters_match_stb` pin them); chunk CRCs are computed eight bytes at a time; map helpers also write indexed terrain and lossless 16-bit gray depth. |
| `font_convert.h/.cpp` | `ConvertFont` validates Unicode metrics and effective kerning, writes indexed PNG atlases; `ConvertFonts` reads installation triplets in deterministic order. |
| [code_pages.h](../../../../src/core/code_pages.h) (`src/common`) | `CodePoint`, `ByteOf`, `Encode`: Windows-1250 and Windows-1252 as Unicode code points and back; shared by the font glyph keys, the text tables, the engine's text loader (`text_load.cpp`) and the GUI layout reader (`gui_layouts.cpp`). |
| `text_convert.h/.cpp` | `ParseTextTable` (the engine's walk of a text file), `ConvertText` and `ConvertTexts`: `text/<lang>.json`, validated ([texts.md](texts.md)). |
| `gui_layout_convert.h/.cpp` | `ConvertGuiLayouts` and `DecodeGuiLayout`: the installed `Data/Script/GUI` scripts as decoded `gui/<name>.gui` and the language choice `gui/layouts.json`, validated ([gui-layouts.md](gui-layouts.md)). |
| `script_convert.h/.cpp` | `ConvertScripts` and `DecodeScript`: the object scripts as `scripts/object/<name>.obs` and each scenario's world script as `scenarios/<id>/world.wsc`, decoded ASCII text with a `#obl-script 1` header ([scripts.md](scripts.md)). |
| `sound_convert.h/.cpp` | `ConvertSamples`: `blsnd.hdr` and `blsnd.spf` as `sounds/<name>.wav`, byte copies checked as RIFF/WAVE ([sounds.md](sounds.md)). |
| `sound_script_convert.h/.cpp` | `ParseSoundScript` and `ConvertSoundScript`: `Sounds.ble` read with the engine's grammar into `sounds/sound_script.json` (`obl.sound_script`), names resolved against the samples and `BLSprite.hdr` ([sounds.md](sounds.md)). |
| `guest_set_convert.h/.cpp` | `ParseGuestSet` and `ConvertGuestSets`: the 15 guest files (`Guests01..12.ble`, the sandbox sets) read with the engine's token grammar into `guests/<set>.json` (`obl.guest_set`), lowercase keys, refusals with byte offsets ([guests.md](guests.md)). |
| `src/formats/schemas.h/.cpp` | Shared with the game (`obl::formats`): `LoadEmbeddedSchemas`, `LoadSchemaDirectory`, `ValidateDocument` and the `obl::json_schema::Registry` over `data/schemas/` validate every JSON document the converter writes. |
| `save_stream.h/.cpp` | Borrowed-byte reader for all old-save record types and the version-1.6 slot header; preserves float bits and undecoded text, checks counts, tags and truncation. `ReadBoundedFile` caps input at 64 MiB; `DescribeOldSave` builds the `save-info` report. |
| `save_body.h/.cpp` | Typed interpretation of the old save body: field tables per emitter (World, managers, every object class and its base classes), one block per section/object in file order, catalog-free consistency checks (`CheckOldSaveBody`: counts, ids, object references); `DescribeOldSaveObjects` builds `save-info --objects` ([saves.md](../saves.md#converting-old-saves)). |
| `save_writer.h/.cpp` | `obl-convert save`: an old save as validated `obl.save` JSON (header, every section label mapped or dropped, grid, managers, objects with keys from `catalog/index.json` and their state); `LoadOblSaveDocument`; the shared `obl.save` contract (`SaveOrigin`, `SaveFloat`, checks) is `src/formats/save_document.h` ([save-conversion.md](save-conversion.md)). |
| `save_encode.h/.cpp` | `EncodeOldSave`: an old save body back to its record bytes, used by old-save conversion and its tests ([save-load-bridge.md](save-load-bridge.md)). |
| `pack_manifest.h/.cpp` | `BuildPackManifest` / `WritePackManifest`: `pack.json` of a converted pack, written last ([pack.md](pack.md)). |
| `sha256.h/.cpp` | Portable SHA-256 (`Sha256`, `Sha256Hex`, `Sha256OfFile`); the unit tests also hash `run/BeachLife.exe` with it. |
| `game_parameters_convert.h/.cpp` | `ParseGameParameters` (the engine's `.pis` grammar, the type letter of each key, the first entry of a repeated key wins) and `ConvertGameParameters`: `rules/game_parameters.json`, validated. The engine's node list is built by `game/game/game_parameters_construct.*` (the data test compares the JSON with it) ([game-data.md](../game-data.md#game-parameters-and-rule-constants)). |
| `media_convert.h/.cpp` | `ConvertMedia`: the soundtrack as `music/<name>.mp3` and the intro movie as `video/intro.mpg`, byte copies checked by their magic bytes ([assets.md](../assets.md#music-and-video)). |
| `cli/main.cpp` | The CLI, one command per line under "CLI commands" below. Exit 0, 1 on an error, 2 on wrong usage. |

CLI commands (one line each, so parallel converter work adds its own line):
- `obl-convert footprints <installation>` lists building key, grid address and size of every grid (keys from the
  installation's `BuildingList`)
- `obl-convert gui-tables <BeachLife.exe>` prints the GUI tables
- `obl-convert identify <installation or CD root>` prints the executable build, the data revision and the identifying hashes
- `obl-convert cabinet list|verify <data1.hdr>` lists the cabinet's files or checks every stored one, `obl-convert cabinet extract <data1.hdr> <group> <dir>` writes one file group out
- `obl-convert sprites <installation or CD root> <out> [--filter <text>] [--jobs <n>] [--schemas <dir>]` writes `<out>/sprites/`
- `obl-convert texts <installation> <out> [--schemas <dir>]` writes `<out>/text/`
- `obl-convert gui-layouts <installation> <out> [--schemas <dir>]` writes `<out>/gui/`
- `obl-convert scripts <installation> <out>` writes `<out>/scripts/object/` and `<out>/scenarios/<id>/world.wsc`, the decoded object and world scripts
- `obl-convert sounds <installation> <out> [--schemas <dir>]` writes `<out>/sounds/` (samples and `sound_script.json`)
- `obl-convert media <installation> <out>` writes `<out>/music/` (every soundtrack track) and `<out>/video/intro.mpg` as byte copies
- `obl-convert game-parameters <installation> <out> [--schemas <dir>]` writes `<out>/rules/game_parameters.json` from `GameParameters.pis`
- `obl-convert guests <installation> <out> [--schemas <dir>]` writes `<out>/guests/` (the 12 campaign and 3 sandbox guest sets)
- `obl-convert catalog <installation> <out> [--schemas <dir>]` writes `<out>/catalog/`
- `obl-convert scenarios <installation> <out> [--schemas <dir>]` writes 24 scenario documents and campaign order; reports ignored PIS entries
- `obl-convert maps <installation> <out> [--jobs <n>] [--schemas <dir>]` writes `<out>/maps/` (islands, water masks and prop key lookup; placements stay in scripts)
- `obl-convert pack <installation> <pack> [--language <lang>] [--schemas <dir>]` writes `<pack>/pack.json`
- `obl-convert import <installation or CD root> <pack> [--language <lang> | --prefer-language <lang>] [--jobs <n>]` runs `ImportPack`:
  every pack command above in the producers' order, `pack.json` last; the browser import runs the same function.
  `--prefer-language` picks among the copy's own tables (`ChooseLanguage`: the installation's, then `<lang>`, then the
  first), so a CD always imports; the Windows installer passes the player's choice
- `obl-convert save-info <save<N>.dat>` reads an old slot to its end and prints the header (floats as value and bits, the title as escaped raw bytes) and record counts per type, with `--objects` the typed sections, objects per class and consistency problems
- `obl-convert save <save<N>.dat> <pack> <out.json> [--schemas <dir>]` writes the converted save

Dependencies: zlib (inflate only, target `obl_zlib` from `cmake/zlib_inflate.cmake`),
`stb_image_write` (in `third_party/`, compiled privately in `png_write.cpp`) and nlohmann/json with the schema validator
(library `obl_json_schema`, split out of `obl_common` so the converter links nothing else of the engine).

Library `obl_convert` (`importer/CMakeLists.txt`, headers included as `"importer/src/<dir>/x.h"`, the public API as `"openbeach/importer/x.h"`), executable `obl-convert`
(`build/x64/bin/<config>/obl-convert.exe`). Unit tests: `tests/unit/convert_*_test.cpp`.

**Browser adapter (`web/`).** The portable part (library `obl_import_web`, unit-tested in every build): `web_source.h`
(`OpenIsoSource`: an ISO 9660 image with Joliet names; `OpenPickedFilesSource`: the files of a picked folder, rooted at
the folder holding `BeachLife.exe` or `data1.hdr`) and `storage_sink.h` (`OpenStorageSink`: the pack under a key prefix
of a `PackStorage`). The Emscripten part: `worker_main.cpp` (`obl_import`, binding those to JavaScript),
`import_worker.js` (the Web Worker: `FileReaderSync` reads, Blobs while converting, OPFS after success),
`shard.js` (the part of the sharded import that does not depend on the host, see "Sharded conversion") and
`node_check.cjs` (the same adapter under Node); `web/CMakeLists.txt` builds `obl-import-web.js/.wasm` in the `wasm` job
(see the browser adapter below).

**Parallel conversion.** The two heaviest steps, `sprites` and `maps`, decode and encode their independent images on worker
threads (`writers/ordered_pool.h`, `std::thread` only). `--jobs <n>` sets the workers; the default is one per hardware
thread and `--jobs 1` converts on the calling thread. The output does not depend on `n`: workers only decode and encode into
memory, and the calling thread commits the results in bank order (sprites) or in the order a serial run does the steps
(maps), so the sink gets the same files in the same order, the first error is the one a serial run reports (the files made
before it are written), and the progress reports are the same sequence. Schema validation, JSON, the file counters and
`PackSink::Write` stay on the calling thread. The memory in flight is bounded: a task is given an estimate of the bytes it holds
(a sprite's biggest animation page and its encoder copies, 256 MB for the planes of an island) and starts only while the
estimates of the tasks held stay within 768 MB (sprites) or 1 GB (maps); a task bigger than that runs alone. The API takes
the count too (`SpriteConversionStats::jobs`, `ConvertMaps(..., jobs)`, `ImportOptions::jobs`; 0 means one per hardware
thread) and defaults to 1. The browser build (Emscripten, no pthreads) always runs serially.

**Sharded conversion (the browser).** The site is not cross-origin isolated, so a page has no threads and no shared memory.
Its import worker (the coordinator) therefore starts *helper workers*, nested Web Workers running the same script, each with
its own wasm instance, and the only thing between them is `postMessage`. The unit of work is the one of the native pool, and
`openbeach/importer/shard.h` names it: a sprite of the bank (`todo` in `SpriteArchive::Convert`) or an image step of the map
writer (`Writer::Step::unit`), numbered in the order a serial run does them. `AssignUnits` gives each unit to a shard,
heaviest first to the least loaded, from the same cost estimates the pool uses, so every instance computes the same split.
`ImportOptions::exchange` carries three modes of the one `ImportPack`, selected by the request's `stage`:
- `prepare` (a CD): the sprite archive is extracted into the spool once, by the coordinator, and kept there
  (`CdSourceLimits::keepSpooled`, `PrepareSpriteData`).
- `shard` (`{"index", "count"}`): the instance runs only the `maps` and `sprites` steps, converts the units it owns and hands each to
  `exchange.sink` as a `UnitOutput` (its files in write order, with the PNGs, the validated JSON, and the error that ended it).
  It writes nothing to the pack, not even a stamp. It stops after its first failed unit, since the import stops there too. A
  CD source adopts the spooled archive of the coordinator (`Spool::Has`) instead of extracting it again; the helpers get the
  spool's Blobs, the picked `File` objects and the same request, and re-open the cabinets only for the small files they read.
- `units`: the coordinator runs the whole import, and the `maps` and `sprites` steps take each unit from `exchange.source` (what
  the helpers sent) instead of converting it. Writing the files, the counters, the first error, both conversion stamps
  (written once, here) and `pack.json` (last, over the whole file set) are the code of the serial path, so the pack is the one a
  single worker writes, byte for byte; a failed unit reports its message after the files it wrote, in unit order, which is the
  error of the lowest-index failing unit.

`shard.js` picks the helpers (`navigator.hardwareConcurrency` minus one core for the page, at most 2; none when
`navigator.deviceMemory` is at most 2 GB; fewer than 2 means none). More do not pay: the sprite step ends with a few large banks
that one helper converts alone, and each helper adds a wasm heap, so 4 helpers took the browser from 3.2 to 4.4 GB and an
Android phone killed the tab. It runs them (`runHelpers`) and holds the
units (`makeUnitStore`, Blobs in the page, bytes under Node). The coordinator mostly waits and adds the helpers' counts, shown
under `maps` until every helper has begun its sprites and under `sprites` after that; the replay in `units` does not report the
steps up to the sprites (`kQuietSteps`), so the bar never goes back.
With one helper, or when a helper fails, dies or cannot start (no nested workers), the coordinator discards the helpers and
imports alone along today's path, so a damaged copy gives a single worker's message. The page cancels by terminating the
coordinator, which ends its helpers. Memory is one wasm heap per helper plus the coordinator's.

**Schemas.** Every JSON is validated against its schema before it is written; an invalid document stops the
conversion with the schema errors (exit 1). The CLI uses its embedded `data/schemas/` bundle by default, so a standalone converter needs no schema files
and works from any directory. `--schemas <dir>` explicitly replaces the bundle with that directory; a missing or invalid
override fails instead of falling back. CMake regenerates the bundle when a schema is edited, added or removed. The
Windows converter, Linux host converter and Linux unit tests all use `cmake/embedded_schemas.cmake`; unit tests compare
every embedded document with the checked-in source and exercise converters against the bundle.


## Topics

| Topic | Summary |
|---|---|
| [tables.md](tables.md) | The tables read from the player's executable: footprints and GUI tables. |
| [cabinets.md](cabinets.md) | Cabinets: contracts, implementation and verification. |
| [assets.md](assets.md) | Sprites, Images: contracts, implementation and verification. |
| [tests.md](tests.md) | Tests: contracts, implementation and verification. |
| [gui-runtime.md](gui-runtime.md) | GUI writer and runtime handoff: contracts, implementation and verification. |
| [fonts.md](fonts.md) | Installation font conversion, Unicode mapping, effective kerning and lossless atlas checks. |
| [gui-layouts.md](gui-layouts.md) | GUI layouts: the engine's choice of script per language, the code-page decoding, `gui/<name>.gui` and validated `gui/layouts.json`; checked by writing the text back to the installed bytes. |
| [guests.md](guests.md) | Guest sets: the engine's token grammar for `Guests*.ble` and the sandbox sets, lowercase keys, the field mapping, refusals with byte offsets; checked field by field against the engine's `WorldParams_Process`. |
| [scripts.md](scripts.md) | Object and world scripts: the installed `.ble` scripts decoded (ASCII, XOR 0xFF) into `scripts/object/<name>.obs` and `scenarios/<id>/world.wsc`, checked by encoding every text back to the installed bytes. |
| [sounds.md](sounds.md) | Sound samples (the `blsnd` index, byte-copy WAV files checked against the engine's bank) and the sound script (`Sounds.ble` with the engine's grammar, checked against the engine's own loader). |
| [texts.md](texts.md) | Installation text tables: the engine's walk of the file, code-page mapping, validated `text/<lang>.json`; checked against the engine's own table. |
| [pack.md](pack.md) | `obl-convert pack`: the manifest `pack.json` (identity, source, default language, per-file SHA-256 and tier), deterministic, validated, written after the other commands. |
| [catalog.md](catalog.md) | `obl-convert catalog`: `BuildingList.ble` and executable footprints into validated JSON. The engine loads the ordered catalog with canonical names, resolved references and checked capacities; tests compare all fixed fields and heap lists with the source parser. |
| [scenarios.md](scenarios.md) | Effective scenario PIS values, ordered campaign and sandbox availability; checked against engine readers for all 24 setups. |
| [maps.md](maps.md) | `obl-convert maps`: terrain, sample heights, lossless RGB565 color and 16-bit depth, water masks and prop keys shared with saves; verified against the engine's loaders. Placements stay in scenario scripts. |
| [save-conversion.md](save-conversion.md) | `obl-convert save`: old save sections and objects mapped label by label into `obl.save` (section tables, list levels, managers, object state layers, keys and object ids). |
| [save-load-bridge.md](save-load-bridge.md) | `obl.save` checks and defaults and the mapping from a document back to old records; pieces and tests. |
