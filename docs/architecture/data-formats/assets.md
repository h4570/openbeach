# Data formats: sprites, images, fonts, audio, texts

Pack formats for sprites, images, fonts, audio and texts. Shared rules are in [principles.md](principles.md).

## Images and pixels
Two pixel kinds cover the source losslessly and leave room for own and HD content:

| Kind | PNG | Use |
|---|---|---|
| `indexed` | 8-bit gray + alpha (color type 4): gray = palette index, alpha = 0 (transparent) or 255 | converted sprites, fonts; exact, because the source's transparency is a run-length property independent of the index (index 0 may be opaque) |
| `rgba` | 8-bit RGBA (color type 6) | own content, mods, HD textures; screens and other true-color images |

- 16-bit source colors (RGB565 maps, 16-bit TGA screens) are expanded to 8 bits per channel by bit replication. The
  engine converts back by truncation (`R >> 3`, `G >> 2`), which returns the source value, so golden images stay
  pixel-identical.
- Haze displacement tables use 8-bit gray PNG (color type 0); values are raw indices, not display colors.
- Masks (pick, shadow) and one-channel planes (sprite depth) use 8-bit gray + alpha with gray as the value.
- 16-bit planes (map depth) use 16-bit gray PNG. `stb_image` reads it; the converter needs a small 16-bit PNG writer
  (around the zlib compressor of `stb_image_write`, which writes 8-bit only).

## Sprites
One JSON per sprite plus PNG sheet pages per animation, in `sprites/` (the converter's mapping:
[converter/index.md](converter/assets.md#sprites)):
```
sprites/gail.json
sprites/gail.palettes.png        palette variants: one row per palette
sprites/gail/d_bob.png           unique frames of one animation (first page)
sprites/gail/d_cabaret_p1.png    further pages of a large animation
```
```json
{
  "format": "obl.sprite", "version": 1,
  "name": "gail", "kind": "character", "tone_group": 3,
  "pixels": "indexed", "palettes": "gail.palettes.png", "palette_colors": 64, "palette_count": 100,
  "animations": [
    { "name": "d_bob", "directions": 8, "frames": 12, "size": [48, 64], "hotspot": [-24, -60],
      "sheets": ["gail/d_bob.png"], "columns": 16, "unique_frames": 70,
      "frame_map": [0, 1, 2, 1, "..."] }
  ]
}
```
- `kind`: `ui`, `character`, `building`, `object_variants`, `special` (the source types 1, 3, 5, 6 and the single
  type-2 sprite; the loader's type 11, absent from the data, needs no kind). `tone_group` replaces the four guest model
  names hard-coded in the engine (palette toning, `PaletteModifiers`); 0 or absent = none.
- `size` and `hotspot` are per animation, shared by its frames, in logical units. Optional `"scale"` for HD sheets.
- The sheets hold the **unique** frames left to right, wrapping after `columns`, and continue on the next page after
  `rows` rows (absent: one page). The converter keeps pages within 4096 x 4096 pixels: one animation of the source
  has 4000 frames of 320 x 240, far beyond a single texture. `frame_map` maps
  `direction * frames + frame` to a unique frame; it is omitted when it is the identity. Lazy per-direction loading
  stays an engine detail (the sheet can be decoded per animation, which is what the source's cache does per block).
- **Planes of buildings.** The source stores them as animations named `image`, `depth`, `lightmap`, `pick`, `shadow`.
  They stay animations with an extra `"plane"`: `color` (indexed), `depth` (gray value), `light` (indexed),
  `pick` and `shadow` (masks, no pixel values). A lightmap keeps its own size and hotspot.
- `depth_extents` (buildings): the four bytes the source stores after the type, one per direction: the extent over
  which construction previews spread the footprint's depth difference (`sprite_preview::DepthPerPixel`). Most are 255;
  29 buildings have others.
- **Palettes:** RGB PNG, width = `palette_colors` (64 for characters and variant objects, which use only the first 64
  colors; 256 otherwise), height = number of palettes. The 555/565 conversion stays in the engine at load.
- **Palette modifiers** (`PaletteModifiers.bin`: twelve 64-color tables) become `sprites/palette_modifiers.png`
  (64 x 12, RGB) with `sprites/palette_modifiers.json` (`obl.palette_modifiers`: the image and the table count only).
  Which tables and palette entries a tone group mixes per palette variant is the engine's own toning rule
  (`palette_modifiers::MakePlan`), not converted data; the pack carries no plan.
- **Tier:** sprite JSON is L1 (frame counts and sizes drive animation timing and hit areas in the simulation); palettes
  and sheets are L2. `palette_count` stays in L1 because variant selection consumes random numbers using it.
  `sprite_pack::ReadSprite` in `ReadMode::Metadata` validates the JSON without opening PNGs
  and returns no palettes; the headless bank uses it. Background rebuilds that still request pixels receive transparent
  run-length frames of the metadata dimensions. The GUI and pixel test drivers load the real PNGs.
  `ctest -R headless_sprite_metadata` (or `python tests/tools/test_sprite_metadata.py`) runs the headless replay in an isolated directory with only sprite JSONs.
- **Run length.** The engine draws run-length frames and some of its blitters see run boundaries (keyline and light
  endpoints, zoomed gaps), so it re-encodes the sheets in the source's form: maximal runs per row, split every
  `run_length` pixels (an optional animation field; 127 when absent, 126 in the animations the source encoded so).
- **Lossless check:** the converter refuses a frame that this encoding (`sprite_pack::EncodeFrame`) would not rebuild byte
  for byte, and the unit test `data.sprite_pack_matches_sprite_bank` re-encodes every frame of the pack against the
  installed archive; the golden images are the end-to-end check.
- `rgba` sprites (mods, HD textures, own UI art) have no `palettes`; palette variants and toning are then unavailable.
  Own character art is indexed, like the source's.

## Other images
| Source | Target | Tier |
|---|---|---|
| `Data/Screens/*.tga` (menus, loading, completion screens) | `images/screens/<name>.png`, indexed PNG with RGB palette or RGB/RGBA | L2 |
| `Data/cursor.tga` | `images/cursor.png` + `images/cursor.json` (`obl.image`: hotspot, optional scale) | L2 |
| `Data/Haze/wibble_*.tif` | `images/haze/<n>.png`, displacement indices with the engine palette | L2 |
| `Data/WaterAnims/WaterAnim<k>.bin` | [maps-scenarios.md](maps-scenarios.md#water-shimmer) | L2 |

## Fonts
Implemented by [`obl-convert fonts`](converter/fonts.md) for installations; runtime loading is still pending.
`fonts/<name>.json` + `fonts/<name>.png` (the atlas, `indexed`: value bit 0 = glyph pixel, bit 1 = outline).
```json
{
  "format": "obl.font", "version": 1,
  "name": "gui_font_8", "size": 8, "bold": false, "atlas": "gui_font_8.png",
  "glyphs": { "0041": [24, 0, 30, 11], "0105": [96, 44, 102, 55] },
  "kerning": [["0054", "006f", -1]]
}
```
- Glyphs are keyed by **Unicode code point** (hex). The source indexes 256 glyphs by byte; the converter decodes each
  font with its code page (`*Pol*` fonts cp1250, the others cp1252), so the engine draws Unicode text. A missing glyph
  draws `?`.
- `kerning` lists only non-zero pairs and holds the **effective** values: today's renderer reads the pair at
  `a * 255 + b` instead of `a * 256 + b` (a source bug kept for identical drawing); the converter writes what that
  lookup returns for each pair, so the engine applies plain pairs and draws the same pixels. Fixing the bug later is a
  data change with re-recorded golden images.
- The Polish and Western fonts become two fonts with different code-point sets; which font a GUI layout uses per
  language is GUI data ([game-data.md](game-data.md#gui-layouts)). A later merged Unicode font is possible without a
  format change.
- Metrics JSON is L1 (the headless host still builds fonts and measures text); the atlas PNG is L2.

## Texts
`text/<lang>.json`, one per language, BCP 47 codes (`en`, `fr`, `it`, `de`, `es`, `pl`):
```json
{
  "format": "obl.text", "version": 1, "language": "pl",
  "entries": [["STRING_IDENTIFIER", "..."], ["MAINSS_SANDBOX_1_TITLE", "..."]]
}
```
- Values are real Unicode. The source Polish table stores cp1250 bytes in UTF-16 units (`a` with ogonek as U+00B9);
  the converter maps them to the proper code points with the language's code page (cp1250 for Polish, cp1252 otherwise),
  using the same rule as `game/ui/text_encoding.cpp` (units up to 0xFF are code-page bytes, higher units are already Unicode).
- **Effective table, in order.** The source reader pairs quoted strings without seeing comments, so its table differs
  from the visible `"KEY" "value"` lines (4342 entries against 4331 lines in Polish). The converter reproduces the
  table the current reader builds, in its order, because an index lookup exists besides the key lookup. Duplicate keys
  stay (lookup returns the first). Once no caller uses the index, a later version may become a key-to-value object.
- Keys of converted texts are source identifiers; they appear only in converted content (scenario files, GUI
  layouts) and never in engine code. Texts the engine itself needs (error dialogs, new UI) use our own keys in an own
  pack (`text/en.json` of the engine pack). The converter may map source keys to ours.
- Tier L1: text lands in saves (feedback messages) and in bot-visible dialogs.
- The engine's text table reads this file for the chosen language (`src/game/tables/text_load.h`); the converter's walk, refusals and undefined
  bytes: [converter/texts.md](converter/texts.md).

## Sounds
The converter's samples: [converter/sounds.md](converter/sounds.md).
- Samples: `sounds/<name>.wav`, one file per entry of `blsnd.hdr`, a **byte copy** of the RIFF/WAVE file inside
  `blsnd.spf` (PCM 16-bit or IMA ADPCM, as in the source); the name is lowercased without `.wav`. No re-encoding.
- The engine accepts WAV (PCM, IMA ADPCM) and MP3 (dr_mp3, already a dependency), nothing else. The L0 pack uses PCM WAV.
- The sound script `Sounds.ble` becomes `sounds/sound_script.json`:
```json
{
  "format": "obl.sound_script", "version": 2,
  "crowd": { "max_number": 55, "max_volume": 1.0, "fade_time": 0.3 },
  "groups": { "voices": { "limit": 3 } },
  "sprite_sounds": { "gail": { "d_bob": { "group": "voices", "sounds": [{ "sample": "splash", "volume": 0.8 }] } } },
  "building_sounds": { "beachbar": { "sample": "bar_loop", "volume": 0.5, "loop": true } },
  "events": { "guest_male_vomit": { "sounds": [{ "sample": "vomit1", "volume": 1.0 }] } }
}
```
  The engine reads this document from the pack (`scripted_sound::LoadDocument`, converter/sounds.md); only v2 is read.
  - `groups` are authored by name, without a count ceiling; `limit` is how many samples of the group play at once (0: none).
    Every referenced group must be declared. A missing `group` means none.
  - `events` bind names to samples, one drawn at random on each firing. The names are open: the game fires the names in
    `scripted_sound::event`, an unknown name stays unused, a fired name the script omits (or binds to no samples) is
    silent and draws no random number.
  - Animations without sounds are simply absent. The script is L1, the samples are L2.

## Music and video
- `Data/Soundtrack/*.mp3` -> `music/<name>.mp3`, byte copy; `music/playlist.json` only if the track choice is data
  (today it is code).
- `Data/Movies/*.mpg` -> `video/intro.mpg`, byte copy (MPEG-1, decoded by pl_mpeg). Own content uses the same codec.
- Both L2.
- Converter (`obl-convert media`): every `Data/Soundtrack/*.mp3` (lowercase names, the file name must be a content key) and
  only the intro movie `Data/Movies/EidosMovie.mpg`, the file the engine plays. Each copy is refused unless its first
  bytes match the kind (ID3 tag or MPEG frame sync for MP3, the MPEG-1 pack header for the movie). The track choice stays
  in code, so no `playlist.json` yet. Tests: `convert.media_*` and `data.convert_media_of_installation`.
- Engine: the playlist's built-in tracks, the night stream and the intro read `packs/base/music/<name>.mp3` and
  `packs/base/video/intro.mpg` (`game/ui/media_pack.h`); a missing file stops the windowed game with a hint to import the data again
  (`game/ui/media_files.cpp`), the headless run boots without them.
