# Converter: assets

## Sprites
`obl-convert sprites` reads `Data/Sprites/BLSprite.hdr` and `.dat` of an installation, or of a CD root (read through
`OpenCdSource`, [cabinets.md](cabinets.md#cd-as-an-installed-tree): the `.dat` is extracted from the cabinets into a
spool file in `<output>.spool/` beside the output directory, removed when the conversion ends; the 467 MB `.dat` is never held in memory). The two must start with the same stamp and
version. The European CD holds the installation's archive byte for byte, so its output equals the installation's; the UK CD
is another revision and gives its own sprites. It writes the format of
[assets.md](../assets.md#sprites).
- Keys: sprite and animation names lowercased; a name that is then not `[a-z0-9_]+` is refused, and so are two sprites
  whose names differ only in case (none in the data).
- `kind` by type: 1 `ui`, 2 `special`, 3 `character`, 5 `building`, 6 `object_variants`; other types are refused.
  `tone_group` 1..4 goes to the guest models: the sprites the guest types of the installed guest sets name (`SPRITE`,
  read from the installation or the CD cabinet), numbered in key order (`ToneGroupsOfGuestSets`); the converter holds no
  model names. The engine has four tone plans, so a fifth model would stay untoned; the shipped sets name exactly
  `fat_bloke`, `fit_bloke`, `gail` and `shaz`, giving the same groups as before. Without guest sets nothing is toned.
  `depth_extents` are the type-5 bytes.
- Palettes: `<key>.palettes.png`, RGB, 64 colors wide for types 3 and 6 (the rest of each stored 256 is unused), 256
  otherwise; one row per palette. `palette_count` records the row count in L1 for simulation variant selection.
  Every sprite of the data has at least one.
- Animations keep their order (the game addresses them by index). Building animations named `image`, `depth`,
  `lightmap`, `pick`, `shadow` get the planes `color`, `depth`, `light`, `pick`, `shadow`; `pick` and `shadow` are masks
  whose runs carry no pixel bytes and decode to gray 0. The same names in other sprite types stay plain.
- A repeated animation name (`dummy` and `shrub09` have one each) gets the sheet name `<name>_<index>`.
- Sheets: gray + alpha, `columns = min(unique, 4096 / width)`, `rows = 4096 / height` per page (`kMaxSheetSize`);
  pages after the first are `<name>_p<k>.png`. `frame_map` is left out when it is the identity.
- Runs: every same-kind run boundary of a frame must follow a run of one length (127, or 126 in some animations), and the
  frame must re-encode byte for byte with `sprite_pack::EncodeFrame`; otherwise the sprite is refused. A length other than
  127 is written as the animation's `run_length`.
- Path tiles: `Data/pathtiles.ble` (installation or CD cabinet) supplies the effective `autotile` table of every
  `pathtile_*` sprite: eight neighbour bits and a frame per record, later duplicates win, frames keep the low byte
  (255 becomes -1), unmentioned masks stay zero, full frame 67. Incomplete records and non-binary neighbours are refused.
- Up to date: a full conversion writes `sprites/conversion.stamp` (output revision `kSpriteOutputRevision`, SHA-256 of the
  header, size of the `.dat`, SHA-256 of `pathtiles.ble`) and skips the work when it matches, since the producers run it on every build. Bump the
  revision when the output changes. Revision 4 can upgrade a matching revision-3 pack by updating only path-tile JSONs,
  keeping its PNGs; changes to `pathtiles.ble` likewise refresh only these JSONs. An interrupted refresh removes the
  stamp, so the next run rebuilds fully.
- Jobs: `--jobs <n>` converts sprites on `n` workers with a bounded memory budget and the same output, errors and progress
  as one job ([index.md](index.md), "Parallel conversion").
- Memory: frames are decoded one at a time into the current page, which is encoded and written when full, so the 32-bit
  converter never holds a decoded animation (4000 frames of 320 x 240 for `gail` `d_cabaret`).

Converting the reference installation with the digital executable produces: 838 sprites, 2920 animations, 584564 unique frames, 4778 files,
447 MB, every JSON valid; about 10 minutes in Debug.
From the CD roots (Debug): the European CD (`iso`) gives the same 4778 files, every byte equal (SHA-256)
to the installation's, in about 9 minutes (the cabinet's `.dat` spool included). The UK CD gives 838 sprites and 4778
files too, of which six PNGs differ from the installation's: the palettes and images of `ui3_fe_ex_back_p`,
`ui3_fe_ex_credits_p` and `ui3_fe_ex_reset_p` (another data revision, `uk-2002-08`; the JSON files agree).


## Images
See [converter-images.md](../converter-images.md) for `obl-convert images`: palette modifiers, indexed/RGB screens, the cursor
atlas and grayscale haze displacement tables, with strict decoding, validated metadata and losslessness tests.

