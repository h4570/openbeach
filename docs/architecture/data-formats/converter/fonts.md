# Converter: fonts

`obl-convert fonts <installation directory> <output directory> [--schemas <dir>]` writes
`fonts/<lowercase-name>.json` and `.png`. It reads the source `Data/Fonts` triplets:
`<name>TexCoord.dat` (2056 bytes), `<name>.dat` (65536 signed kerning bytes), and `<name>.tiff`.
Lookup is case insensitive; duplicate names, missing companions and empty font directories fail.
The engine loads these files through `font_pack::ReadFont`;
missing or invalid files stop the game with their path and reason; missing files include a setup hint.

## Conversion contract

The metrics preserve size, bold and the 256 glyph rectangles. Rectangles must be ordered and fit inside the atlas.
Each byte becomes a lowercase hexadecimal Unicode key through Windows-1250 for names containing `pol`
(case insensitive), or Windows-1252 otherwise. Undefined code-page bytes are omitted rather than merging
several glyphs into U+FFFD; the runtime's missing-glyph fallback remains `?`.

Kerning contains only non-zero signed pairs, with the effective byte at `a * 255 + b`, preserving today's
renderer lookup. JSON is validated through `json_schema::Registry` before either output is written.
Metadata is L1, the atlas is L2; see [../assets.md](../assets.md#fonts).

The TIFF reader accepts little-endian, top-down, single-channel palette TIFFs with 4-bit or 8-bit samples,
even widths for packed 4-bit rows, strips and uncompressed or PackBits data. It rejects unsupported layouts and malformed strip ranges
or runs. High nibble comes first. Pixel values 0..3 retain the glyph and outline bits in PNG gray+alpha;
alpha is zero for index 0 and 255 otherwise. Other pixel values fail. No palette colors enter the atlas.

Output order is deterministic by lowercase font name. An I/O failure can leave a partial output directory;
discard or regenerate it before use. Converted content stays outside git.

## Verification

`convert.font_unicode_effective_kerning_and_pixels` uses invented metrics, kerning and TIFF pixels to check
Polish/Western mapping, signed effective kerning, PNG round trips and truncated input.
`data.convert_fonts_match_engine_loaders` converts every installed font, loads it through the runtime reader,
and compares every defined glyph rectangle, effective kerning pair and atlas pixel with the current engine inputs.
Existing image tests cover the shared strict TIFF reader's malformed runs.

The photo mode's fonts `Photo16` and `PhotoPol16` are ordinary triplets: `photo16` takes Windows-1252 and `photopol16`
Windows-1250 by the `pol` rule. Which of them a language uses is not in the data yet (`gui/layouts.json` has no
photo role).
