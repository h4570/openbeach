# Converter: cabinets

## Cabinets
The CDs store the game in InstallShield 6 cabinets: the header `data1.hdr` (file table, directories, file groups) and
the volumes `data1.cab`, `data2.cab` (stored bytes). The reader supports exactly what the CDs use and refuses the
rest:
- Header version `0x0100600C` with the `ISc(` signature only, in the header and in every volume it opens.
- File descriptors of 0x57 bytes; flag 4 is compressed, 8 an invalid placeholder (dropped: its name is garbage).
  Split (1) and obfuscated (2) files occur on neither CD and are refused; the stored range must lie inside its
  volume, so a file never silently continues in the next one.
- Compressed data is a sequence of chunks, a `u16` length then an independent raw deflate stream (ending on a sync
  flush, not necessarily a final block). Extraction streams chunk by chunk (the largest file is 467 MB) and checks
  the listed size and the stored MD5 at the end.
- An uncompressed entry at offset 0 refers to an *external* file beside the cabinets, so the reader does not extract it.
  Examples include setup files, readmes and `BeachLife.exe`; the executable's listed size of 1,613,824 differs from its actual size.
- Names are Windows-1252 (`Léeme.htm`), turned into UTF-8 with `/` separators; lookups ignore ASCII case. A path
  in more than one group is ambiguous and refused (the language variants of `BeachLife.ini` live in each `Ess_*`
  group).

The game files are the groups `EURO Comp` (European CD) and `UK Comp` (UK/US CD). Verified:
`obl-convert cabinet verify` passes on both CDs (337 and 305 stored files, about 12 s each in Debug), and the 201
files extracted from `EURO Comp` match the reference installation's European data byte for byte.

## CD as an installed tree
`OpenCdSource` (`src/source/cd_source.h`) is a `ByteSource` presenting a CD root (a folder or an ISO image's source) as
the tree it would install, so every converter and `ImportPack` read a CD like an installation; `IsCdRoot` (a
`data1.hdr` but no installed `Data/Sprites/BLSprite.hdr`) decides when `ImportPack` and the CLI's per-step commands wrap a
source in it.
- The tree is the stored files of the group holding `Data/Sprites/BLSprite.hdr` (`EURO Comp`, `UK Comp`; no group names
  are hard-coded) plus the loose `BeachLife.exe` of the CD root. `BeachLife.ini` is not presented (one variant per
  `Ess_*` group), so a CD import names its language (`--language`, the request's `language`).
- Reads extract the whole file once: up to `CdSourceLimits::memory` (64 MB) into an in-memory cache with that byte
  budget (most recently read first), larger files (only `BLSprite.dat`) into a `Spool` (`openbeach/importer/spool.h`)
  the caller provides. Every extraction is MD5-checked; a failed one reads as missing and leaves no spool file. The spool
  files are removed with the source.
- Spools: `OpenDirectorySpool` (CLI: `<output>.spool/` beside the output or pack directory, never inside it, since `pack.json` lists every file of the pack directory), `OpenMemorySpool` (tests), and in the browser
  `JsSpool` (`web/worker_main.cpp`), which appends to a Blob in 16 MB pieces and reads ranges with `FileReaderSync`, so
  the 467 MB archive never sits in the WebAssembly heap.

Unit test `convert.cd_source_presents_the_installed_tree` (synthetic cabinet: tree, ranged reads in memory and spooled,
spool cleanup, MD5 failure); `data.convert_sprites_of_cd_equal_installation` converts sprites from the European CD
through it ([assets.md](assets.md#sprites)).

