# Asset recipes

Mods cannot ship art derived from the player's copy of the game. Instead, a mod may ship `recipes/<name>.json` (`format: "obl.asset_recipe"`, schema `data/schemas/asset_recipe.schema.json`): a logical source PNG from the player's converted pack, an ordered list of deterministic integer/fixed-point image operations, and the output logical path inside the mod.

```json
{
  "format": "obl.asset_recipe",
  "version": 1,
  "source": "images/screens/l0_title.png",
  "output": "images/screens/l0_title_wide.png",
  "operations": [
    { "op": "crop", "x": 0, "y": 0, "width": 64, "height": 48 },
    { "op": "mirror", "axis": "horizontal" },
    { "op": "scale", "width": 96, "height": 48, "filter": "bilinear" },
    { "op": "blur", "radius": 2, "passes": 2 },
    { "op": "pad", "width": 128, "height": 48, "mode": "mirror" }
  ]
}
```

## Fields
- `format`: `"obl.asset_recipe"`, `version`: `1`.
- `source`: logical path of an 8-bit PNG inside the pack (for example `images/screens/title.png`). Indexed, gray, gray+alpha, RGB and RGBA PNGs are decoded to 8-bit RGBA.
- `output`: logical path inside the mod — either a `.png` image (for example `images/screens/title_wide.png` or `overlay/banner.png`) or `sprites/<key>.json` for an indexed `obl.sprite`. Cannot target `mod.json`, `recipes/` or `recipe-cache.json`.
- `operations`: 0 to 32 image operations applied in order. No arbitrary code; audio and text recipes are not supported.
- `sprite` (optional, only when `output` is `sprites/<key>.json`):
  - `kind`: `"ui"` (default), `"character"`, `"building"`, `"object_variants"` or `"special"`.
  - `animation`: animation key (default `"idle"`).
  - `hotspot`: `[x, y]` integer hotspot (default `[0, 0]`).
  - `palette_colors`: `64` or `256` (default `64` for `"character"`, `256` otherwise).

## Operations
All operations use integer or 16.16 fixed-point arithmetic only, so the same source and recipe produce identical bytes on Windows, Linux and WebAssembly. Dimensions are capped at 4096 pixels per side.

| `op` | Fields | Behavior |
|---|---|---|
| `crop` | `x`, `y`, `width`, `height` | Extracts a rectangle inside the current image bounds. |
| `mirror` | `axis`: `"horizontal"` \| `"vertical"` | Flips pixels horizontally or vertically. |
| `scale` | `width`, `height`, optional `filter`: `"bilinear"` (default) \| `"nearest"` | Resamples using 16.16 fixed-point bilinear interpolation or integer nearest-neighbor. |
| `blur` | `radius` (`1..64`), optional `passes` (`1..5`, default `1`) | Separable integer box blur with clamped edges and round-to-nearest division. |
| `pad` | `width`, `height`, optional `x`/`y` (default centered), optional `mode`: `"color"` (default) \| `"edge"` \| `"mirror"`, optional `color`: `[r, g, b, a]` (default `[0, 0, 0, 255]`) | Places the image on a new canvas, filling outside pixels by solid color, clamped edge pixels, or reflected pixels. |

## Outputs
- **PNG (`.png`):** encoded deterministically with `EncodePng` (8-bit RGB when every pixel is opaque, 8-bit RGBA otherwise).
- **Sprite (`sprites/<key>.json`):** quantized deterministically (exact palette when at most `palette_colors` unique opaque colors exist, else integer median-cut) into `sprites/<key>.palettes.png`, `sprites/<key>/<animation>.png` (gray+alpha), and a validated `obl.sprite` JSON document readable by `obl::sprite_pack`.

## Running, caching and missing sources
- **CLI:** `obl-convert recipes <pack dir> <mods dir> [--mod <id>] [--cache <dir>] [--schemas <dir>]`. `obl-convert import` also runs recipes for installed mods sitting beside `packs/` after the pack import, without writing into the pack directory.
- **Mods screen:** installing a ZIP calls the same recipe builder on every platform against the current pack.
  Replacing or removing a mod clears its obsolete outputs. The running browser game's file bridge saves these outputs to OPFS.
- **Browser importer:** `import_worker.js` runs installed mods' recipes after a pack import and accepts `{type: "recipes", root?, mods?}` (via `obl_build_mod_recipes`) when a mod is installed or updated.
- **Cache:** outputs are written to `<game dir>/mod-cache/<id>/` next to `mods/<id>/`, with `recipe-cache.json` recording each recipe's SHA-256 and its source's SHA-256. A recipe re-runs only when its JSON, its source file, or a cached output changes; stale outputs from removed or disabled recipes are deleted.
- **Missing source:** when `source` does not exist in the pack (for example on another pack or an L0-only install), only that recipe's output is disabled and a warning message is reported; other recipes still build.
- **Layer order:** `pack_path` resolves an active mod's shipped files (`mods/<id>/`) first, then its cached recipe outputs (`mod-cache/<id>/`), then earlier mods and the pack. Presentation scripts (`obl.overlay.image`) also fall back to `mod-cache/<id>/overlay/`. Because outputs live outside `mods/<id>/`, `ModContentHash` is unchanged across packs.

## Scope rule
A presentation mod (`"scope": "presentation"`) may ship `recipes/<name>.json` only when `output` is a presentation-only image (`overlay/<name>.png` or `images/screens/<name>.png`). Screen pictures and overlay images are drawn only by the UI and never reach the simulation or its RNG. Recipes targeting sprites or other pack files require `"scope": "world"`.

## Tests
- Unit tests: `convert_recipe_test.cpp` (every operation, determinism, caching, missing source, indexed `obl.sprite` round-trip), `pack_path_test.cpp` (`mod-cache` layer order), `mod_pack_test.cpp` (presentation recipe scope), and `importer/web/node_check.cjs` (WebAssembly `obl_build_mod_recipes`).
- Game test: `tests/l0_mod_recipe.txt` with `tests/mods/l0_recipe` (derives `overlay/banner.png` from L0's `images/screens/l0_title.png` and includes a missing-source recipe that disables only its own output).
