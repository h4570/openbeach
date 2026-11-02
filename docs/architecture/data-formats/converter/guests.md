# Converter: guest sets

`obl-convert guests <installation directory> <output directory> [--schemas <dir>]` writes `guests/<set>.json` for the
twelve campaign files `Data/Script/Guests01.ble` ... `Guests12.ble` (`campaign_01` ... `campaign_12`) and the three sandbox
files `SandboxE_Guests.ble`, `SandboxM_Guests.ble`, `SandboxH_Guests.ble` (`sandbox_easy`, `sandbox_medium`, `sandbox_hard`).
The format is `obl.guest_set` ([game-data.md](../game-data.md#guest-sets)). Files are matched ignoring ASCII case, so
`guests03.ble` and `SandboxE_guests.ble` resolve as installed.

## Grammar

The file is every byte XOR 0xFF, then read as the engine's `WorldParams_Process` reads a gameplay file
(`importer/src/readers/world_params_process.h`, with the field writers of `importer/src/readers/world_params_add.h`):

- Bytes outside double quotes are ignored (comments, blank lines). The file is a sequence of quoted tokens; an odd
  number of quotes is refused.
- `"GUEST_<key>"` starts a guest type. A token `"<key>_<FIELD>"` sets one field of the guest with that key; the key ends
  at the first `_` (except after the prefixes JETTY and SECURITY, as in the engine). The value is the next token, or the
  next two for the range fields (`SLEEPTIME`, `WAKETIME`, the `*PREF`, `*TOL`, `*_EFFECT` and `*_START` pairs).
- `"SURNAME"`, `"MALENAME"` and `"FEMALENAME"` followed by a text key append to the name pools.
- `NAMESET` is read and ignored, as the engine ignores it. The pools are global to the file, so the name set of a
  guest is not in the record.

The converter refuses what the engine would not load as written: an unknown keyword or field, a field before its
`GUEST_` line, a token without its value, a repeated guest key, a guest without `GENDER`, a `GENDER` other than `M` or `F`,
a `DRUNK_TYPE` other than `TIRED`, `HORNY`, `FIGHT` or `VANDALISE`, a `DRUNKTYPE` outside 0..4, a number that is not finite,
a `NAME` or `SPRITE` longer than the engine's slot, a text key that is not `[A-Za-z0-9_. -]` or longer than 63 bytes, more
than 20 guests, more than 140 male names, 140 female names or 600 surnames. The refusal names the byte offset.

## Mapping

- `key` and `sprite` are lowercase, as in the catalog (`FSunbather` -> `fsunbather`, `Fit_Bloke` -> `fit_bloke`). Keys
  must be content keys, and no two keys may be equal after lowercasing. The engine looks guest keys and sprites up
  without case, so set version 2 carries no source spelling.
- Numbers are read with `atoi` and `atof`, as the engine reads them; floats are stored as float32, so the document
  holds the value the engine holds, written as the catalog writes its floats (`FloatNumber`: the shortest decimal of the
  float32). Byte fields (`COLOUR`, `NIGHTS_MIN`, `NIGHTS_MAX`) wrap as the engine's byte stores do.
- `drunk_type` is the engine's code: 0 `none`, 1 `tired`, 2 `flirt` (the source's `HORNY`), 3 `fight`, 4 `vandalise`.
- The pairs are in the engine's index order: preferences `alcohol`, `amusement`, `meals`, `party_fun`, `snacks`,
  `soft_drinks`, `souvenirs`, `sports_fun`, `sunbathe`, `swimming`, `water_fun`, `stroll`, `sleep`; the opinions
  `beauty`, `crowding`, `attractiveness` (the source's `TOTTY`), `price`, `variety`, `cleanliness`, `safety`.
- `max` names the 26 `MAX_` actions in the engine's order; the source's `HARDSEX` is `intimacy`.
- Scalars (`name`, `gender`, `sprite`, `default_sprite`, `colour`, `drunk_type`, `money`, ..., `nights`, `sleep_time`,
  `wake_time`) are always written. A member of a group (`preferences`, `tolerances`, `opinion_effects`, `opinion_start`,
  `start`, `decay`, `max`) is written only when its bits are not zero, so a -0.0 is kept.
- `name` is the `NAME` text, which is empty for every guest of the installation: the names come from the pools.

## Verification

`convert.guest_set_parses_the_engine_grammar` parses a small invented file and checks the mapping, the byte wrap, the
pairs and the pools, and validates the document. `convert.guest_set_refuses_what_the_engine_refuses` checks the refusals
above on invented texts, the guest and name limits included.

`guest_set.load_matches_engine_on_a_sample` and `guest_set.load_refuses_transactionally` (`tests/unit/guest_set_load_test.cpp`)
check the loader (`src/game/tables/guest_set_load.cpp`) against the engine's parser on the invented sample, and refusals leave the
guest part of the record unchanged. `guest_set.names_map_to_their_sets` checks the shared table (`src/game/objects/guest_sets.h`)
that maps a scenario's `WORLD_GUESTLIST` to a set.

`data.guest_set_load_matches_engine` converts the installation, loads each set with the loader and compares the guest
records and name pools byte for byte with the engine's parser (`importer/src/readers/world_params_process.h`); it skips without an
installation.

`data.convert_guest_sets_match_engine` converts the installation, reads back the 15 written files, and loads the same
bytes with the engine's own `WorldParams_Process`. It compares every record and every
field of `GuestDef` (zeros included, floats by their bits), and the three name pools with their counts. It skips without an
installation.
