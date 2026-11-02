# Converter: palette modifiers and images

`obl-convert images <installation> <out> [--schemas <dir>]` writes palette modifiers and all screens, the cursor
atlas and the six haze images. Paths are resolved ignoring ASCII case; screen keys are lowercased, sorted and checked
for duplicates and unsafe characters. It stops at the first error (exit 1). As with sprites, a failed conversion can
leave a partial output directory; discard or regenerate it. Output belongs only in ignored local data, never in git.
The command takes an installation; extracting a CD's game group first also works. Reading images directly from
cabinets remains part of the later CD asset task.

- `Data/Sprites/PaletteModifiers.bin`: exactly 2304 RGB bytes become `sprites/palette_modifiers.png` (64 x 12 RGB)
  without quantization. `sprites/palette_modifiers.json` validates as `obl.palette_modifiers`; tone groups 1/2 start
  at tables 0/4, groups 3/4 share table 8. Variants and optional extra ranges come from the existing toning plan.
- `Data/Screens/*.tga`: `images/screens/<key>.png`, preserving the engine's pixel depth, indices and RGB palette.
- `Data/cursor.tga`: `images/cursor.png` keeps the complete indexed atlas (32 x 32 cells, shapes across, frames down).
  Index zero remains the renderer's transparent index; duplicate palette colors retain distinct indices.
  `images/cursor.json` validates as `obl.image`, names `cursor.png` and records hotspot `[0, 0]` (no origin offset).
- `Data/Haze/wibble_00..05.tif`: `images/haze/0..5.png` preserves displacement indices and the engine's palette.

The runtime's `image::PackPath` routes only these three image families to `packs/base/images/`; fonts retain their
separate loader. `image::LoadPng` restores the engine buffers without opening the installed files. Missing or
rejected converted files stop the game with the pack path and setup command. All three pack producers run `images`.

TGA reader: indexed 8-bit with 24-bit palette, true-color 16/24/32-bit, uncompressed or RLE, all four origins.
It checks dimensions (at most 16 million pixels), palette indices and packet bounds; unsupported types and layouts
fail. File IDs are skipped; a TGA footer may follow the pixels. TIFF reader: little-endian, indexed 8-bit single
sample, top-left, strips with no compression or PackBits. Tags, offsets, counts, complete strip coverage and run
bounds are checked. Tiles, unsupported compression, alternate orientation and duplicate relevant tags fail rather
than emitting blank pixels. Input files are capped at 64 MiB. The strict export reader validates input before the
engine decoder supplies the exact buffer representation;
layouts the engine decoder cannot represent are refused by the installation conversion. `ConvertAssetImage` remains
the RGBA preview export helper; runtime PNGs preserve the engine representation.

`convert.image_*` uses invented RGB tables, palette TGA images and TIFF displacement fields: PNG round trips,
all tone variants, cursor transparency, row orientation, RLE, true-color channels, every prefix truncation and
malformed runs/tags. Schema and output sink failures are covered. `data.convert_images_of_installation` converts
the full installed image set under `run/tmp/`; `data.convert_image_pixels_match_engine_decoders` compares each
screen, cursor and haze export with the existing engine decoder's pixels.
`data.convert_images_match_engine_loaders` compares dimensions, depth, pixels and every palette byte after
loading the converted files through the runtime PNG reader. Installation tests skip without installed data.

The reference installation with the digital executable yields: 17 screens, the cursor atlas, six haze tables and palette modifiers,
27 output files. All image pixels match the existing engine decoders; both JSON documents pass the real schemas.
