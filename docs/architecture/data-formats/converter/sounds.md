# Converter: sounds

`obl-convert sounds <installation directory> <output directory> [--schemas <dir>]` writes `sounds/<name>.wav` for every
sample of the installation's `Data/Sounds/blsnd.hdr` and `blsnd.spf` ([assets.md](../assets.md#sounds)) and
`sounds/sound_script.json` for `Data/Sounds/Sounds.ble`. Each sample is a byte copy of its RIFF/WAVE bytes; nothing is
re-encoded.

## Index and names

- `blsnd.hdr` is a list of 40-byte records: a little-endian offset and size into `blsnd.spf`, then a 32-byte name padded
  with NUL. The engine does not read these records: `audio::SoundBank::Open` (`src/game/ui/sound_bank.cpp`) takes every
  `sounds/<name>.wav` of the pack as one entry, keyed by the upper-cased `<name>.WAV` the engine's lookups use.
- The name is ASCII, without the `.WAV` suffix (any case), lowercased. It must be a content key (lowercase letters,
  digits and `_`), so `sounds/<name>.wav` is a valid path in the pack.
- The converter refuses: a file that is not whole records, a name that is empty, not ASCII, not a key, or repeats after
  lowercasing, a zero size, and a sample that runs past the `.spf` file.
- Each sample is read as one slice of the `.spf` file (the file is about 32 MB and is never held whole). The slice must be
  a RIFF/WAVE file whose RIFF size plus 8 is its length; any other slice is refused.
- Output order follows the index. Samples are L2: their bytes never reach the simulation.

## Verification

`convert.sample_index_parses_and_names_samples` and `convert.sample_index_refuses_bad_records` run invented indices
through `ParseSampleIndex`; `convert.wave_file_size_must_match` checks the RIFF rule.
`data.convert_samples_of_installation` converts the installation and opens the output with `audio::SoundBank::Open`. The
reference is a test-local copy of the old reading of `blsnd.hdr` and `blsnd.spf` (keys, sizes and slices), not the
converter's parser: the key sets must be equal, and every entry's size and bytes must match. It skips without an
installation.

## Sound script

`Sounds.ble` (every byte XOR 0xFF) becomes `sounds/sound_script.json` (`obl.sound_script`, [assets.md](../assets.md#sounds)).
Only the converter reads this source grammar:
whitespace and comments (a `/` at a token boundary runs to the end of the line), `;` as its own token, a token ends at
whitespace, `;` or NUL, and at most 51 bytes. The commands are `SpriteSounds`, `BuildingSounds`, `CrowdSound`,
`OpinionSounds` and `Groups`, each closed by `End`; the command names and the crowd keys ignore case.

- A sprite name and its animation names are resolved against `Data/Sprites/BLSprite.hdr` (case-insensitive, as the
  engine's `FindAnimation`); a sample against the converted samples (`blsnd.hdr`). Missing names are refused.
- The document keeps one entry per sprite, animation, building and opinion, so a repeated one is refused.
- `kKeywordEvents` (`sound_script_convert.cpp`) is the only map from source opinion keywords to event names
  ([assets.md](../assets.md#sounds)); it ignores case, and any other keyword is refused. Shipped scripts list the keywords
  in event order, so name dispatch equals the earlier positional dispatch (`data.convert_sound_script_matches_source`).
- A source group id `n` (0..19, the source's range) becomes the group `group_<n>`, declared ones first in file order. A
  group used but never limited plays nothing in the source; it is declared with limit 0 after them, so group indices and
  owner ids stay those of the source on the shipped script.
- Source limits: 30 sprite sections and 20 buildings (the engine's record arrays). A `Group` before a count is
  optional; without one the entry has no `group`.
- Volumes and crowd values are the number the token reads (`atof`); the engine stores them as floats, so the test
  compares float values.

The engine reads the converted document: `ScriptedSound::Load` (`game/ui/scripted_sound_runtime.cpp`) reads
`sounds/sound_script.json` of the pack and `scripted_sound::LoadDocument` (`game/ui/scripted_sound_pack.cpp`) builds the
records, sections in the document's order (`ordered_json`). The engine matches sprite, animation and building names
without case, event names exactly. A missing document, a version other than 2 or an undeclared group stops the game.

The sample bank reads the same pack: `SoundBank_Construct` and `SoundBank_LoadHeader` (`game/ui/sample_mixer.cpp`) open
`packs/base/sounds/` and keep every `.wav` file of it (other files, the script included, are not entries). Bytes are read on
first use and dropped after five idle seconds, as before. A missing directory, one without `.wav` files, or an empty `.wav`
stops the game. The engine's `Sound_Construct` ignores its bank name: the converter writes one set of samples.

`data.convert_sound_script_matches_source` compares the converted document with a test-local reading of the same text
(names, groups, volumes, keyword-to-event bindings). `convert.sound_script_parses_the_grammar` and
`convert.sound_script_refuses_bad_scripts` check the grammar and the refusals on invented scripts.
