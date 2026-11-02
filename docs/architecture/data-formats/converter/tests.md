# Converter: tests

## Tests
`data.convert_original_executables` is the oracle on a local reference installation: it finds it (next to the checkout or
through `$OBL_OGDATA`), expects a digital executable with European data,
then compares both CDs' footprints with that reference (same keys and order, exactly four known
grids differ); it skips without the installation (CI, Linux).
`convert.identify_*` cover hashes, stamps and the case-insensitive lookup; `convert.sha256_fips_vectors` the digest.

`convert.*` cases of `convert_exe_tables_test.cpp` build a synthetic PE in memory (`SyntheticExe` in the test): our own keys `KEY_00`..`KEY_47` and grids,
code bytes in both shapes, grids in another order than the code assigns them, and variants with shifted data and
data-only section flags. Negative cases: missing anchor, a missing store, a duplicate key, an implausible size, a key
that is not an identifier. They hold no source bytes (provenance rule 5).

`convert_gui_tables_test.cpp` does the same for the GUI tables: a synthetic PE with our own records (only the list
anchors and `UI3_ob_goto` are the real keys), both list loop shapes, decoy row button tables and three layouts; negative
cases for a duplicated heading lookup, a broken heading, two guest lists, a missing `NONE` and a misaligned credits
table. `data.convert_gui_tables_match_gui_code` converts `run/BeachLife.exe`, checks that `gui_content::Parse` gets back
exactly the converter's records (every list, heading, row button and credit) and that our filter definitions cover
every heading's filter, without repeating any extracted value;
`data.convert_gui_tables_of_cd_executables` compares the CD executables in the reference installation with the main one (equal
except the aerobics event). `tests/unit/og_data_dir.h` finds the installation for the oracle files.

`convert_cabinet_test.cpp` builds a synthetic header and volume with our own names and contents, compressed as stored
deflate blocks (one sync-flushed and one final chunk per file): listing, groups, a Windows-1252 name, case-insensitive
lookup, extraction of compressed and plain files; negative cases for an ambiguous path, an external entry, another
version or signature, a split file, a range outside the volume, a damaged byte (MD5) and a missing volume.
`convert.md5_rfc1321_vectors` covers the digest.

`data.convert_sprites_of_cd_equal_installation` converts `ui3_bi_help` and `accomblk` from the European CD (disc image,
the `.dat` spooled from its cabinet once for both filters) and from the installation, and compares the file sets and every
byte; about 8 s in Debug. The full-bank comparison stays a manual run (below).

`convert_sprite_test.cpp` builds a synthetic archive (a character with two palettes and a repeated frame, a building
with a color plane and a pick mask): every truncation of the header, a trailing byte and an unknown type are refused;
the decoder refuses a run past the width, an empty run, a frame index outside and data that ends early. The converted
PNGs are decoded again and compared with the source pixels and palettes; the JSON is validated with the real schemas.
`convert.sprite_sheet_pages` covers frames of 1500 x 1500 spread over two pages. `data.convert_sprites_of_installation`
converts `ui3_bi_help`, `accomblk` and `blueherring` (a prop character: kind `character`, no tone group) from `run/` into a per-run temporary directory, so parallel runs do not delete each other's output. The guest models stay out of the suite (`shaz`: tone group 4, 100k frames, about 200 s in Debug); tone groups are checked by the synthetic `convert.sprite_*` tests. The whole bank stays a manual `obl-convert sprites` run. `data.convert_cd_sources` identifies both CD roots in the reference installation
through their cabinets, expecting the August CD build with European and UK/US data respectively. It also compares
the European CD's sprite-header hash with the installation's; the full sweep stays in `obl-convert cabinet verify`, outside ctest.

