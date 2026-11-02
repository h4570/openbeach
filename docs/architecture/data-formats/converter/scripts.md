# Converter: object and world scripts

`obl-convert scripts <installation directory> <output directory>` writes the installed scripts as decoded text in their
existing syntax ([game-data.md](../game-data.md#scripts)):

| Installed | Output |
|---|---|
| `Data/Script/Object/<name>.ble` (8 files: guest, staff, shark behavior) | `scripts/object/<lowercased name>.obs` |
| `Data/Scenario/Scenario<n>/*.ble` (one per folder) | `scenarios/s<n>/world.wsc` |
| `Data/Scenario/SandScenario<n>/*.ble` (one per folder) | `scenarios/sandbox<n>/world.wsc` |
| `BeachLife.exe` state and message tables | `scripts/object_vocabulary.json` ([below](#vocabulary)) |
| `BeachLife.exe` message table | `scripts/world_vocabulary.json` ([below](#vocabulary)) |

The scenario ids are the ones of [maps-scenarios.md](../maps-scenarios.md#scenarios) and the replay header. Object names
are lowercased because pack paths are lowercase ([pack.md](pack.md)). `.pis` files of the scenario folders are not read
here: they belong to the scenario item (`scenario.json`), not to the script.

## Conversion contract

- Each byte is XOR-ed with 0xFF. The installed scripts are ASCII; a byte outside ASCII (a UTF-16 byte-order mark among
  them) stops the conversion with its position, instead of a guessed code page. The text keeps every byte, CRLF line
  ends included; nothing is trimmed.
- Each output file is the first line `#obl-script 1 object` or `#obl-script 1 world`, then the decoded text. The source
  scripts have no header; the readers check it.
- Object scripts are listed in sorted order. A scenario folder must hold exactly one `.ble`; a folder name without a
  scenario id (`Scenario<n>` or `SandScenario<n>`) is refused, as is an installation without `Data/Script/Object` or
  `Data/Scenario`.
- No JSON is written, so nothing is validated against a schema and `--schemas` does not apply.
- Tier: the default L1 of `obl-convert pack` (the headless simulation runs the scripts).

## Loader: engine paths map to pack files

The conversion keeps the names of the source paths byte for byte, because the round trip must hold. The reader maps them
(`script_pack::PackPath`, `formats/script_pack.cpp`); the engine paths are never rewritten in the data:

- an object logic `data/script/object/<name>.lgc` (a world script's `LoadScript GuestLogic data/script/object/guest.lgc;`,
  `ScriptProgram_Load`) is `scripts/object/<name>.obs`, the name lowercased;
- a scenario's script `Data/Scenario/<folder>/<folder>.lgc` (`WORLD_SCRIPT` of the `.pis`, `WorldScript_Load`) is
  `scenarios/<id>/world.wsc`, the id from the folder ([maps-scenarios.md](../maps-scenarios.md#scenarios)).

Slashes and backslashes both separate the parts, and case is ignored. A path of the other kind is refused.
`script_pack::ReadScript` checks the header line of the file, returns the text after it and refuses a missing file, a wrong
header or an empty text, each with the file name and the hint to import the game data again; there is no fallback to the
installed `.ble`. The loaders add the `.` terminator the parsers read past the text. The world loader returns the length of the
text (the source `.ble` size), which starts the loading bar.

## Vocabulary

The names the parsers resolve come from two places:

- **The engine's script API** (in code, not converted): the built-in functions (`Function <name>;`) and the values
  (variables and constants of conditions and assignments) the engine implements, each with the number the parser stores
  and the object or world code dispatches on: `script_program::EngineFunctions`/`EngineValues`
  (`game/objects/script_program_api.cpp`, 179 functions, 223 values) and `world_script_parse::EngineFunctions`/`EngineValues`
  (`game/world/world_script_api.cpp`, 9 functions with the `world_control` numbers, 35 values). The tables are sorted by
  number and unique ignoring ASCII case (the parsers compare that way); one number may have two names (139:
  `Move2SeaEntryPoint`, `FnMove2SeaEntry`). A new function or value is added to the table together with its
  implementation.
- **The state and message tables of the user's `BeachLife.exe`**, written by the converter to
  `scripts/object_vocabulary.json` (format `obl.object_script_vocabulary`, version 1: `states`, `messages`, each row
  `{id, name}` in the order the executable stores them) and `scripts/world_vocabulary.json` (format `obl.world_script_vocabulary`,
  version 1: `messages`). The loaders read these files and add the API (`ParseScriptIds`,
  `game/objects/script_program_ids.cpp`; `ParseWorldScriptIds`, `game/world/world_script_load_ids.cpp`); a missing file
  stops the game. The world loader needs exactly 108 messages. Source: `importer/src/readers/script_vocabulary.cpp`.
- **The feedback vocabulary**, written by the same `scripts` step to `scripts/feedback_vocabulary.json` (format
  `obl.feedback_vocabulary`, version 1; `FeedbackVocabularyJson`, `importer/src/writers/script_convert.cpp`): `commodity_keys`
  (the complaint text key of each of the 26 commodity families), `message_types` (the `MESS_TYPE_*` words of the scenario scripts
  with their message type number, or `silences` `world`/`object`), `missing_staff` (per staff class name, in check order: the
  notice key and the message-type word of the "no <profession>" notice) and `professions` (the hire command's names with
  their `hire_type`, 0..5, the staff browser's button). `ParseVocabulary` (`game/feedback/feedback_vocabulary.cpp`) reads it from
  `LoadFeedbackVocabulary`; a missing or refused file stops the start. The engine keeps the staff class list
  (`game/objects/staff_classes.h`) and the mechanism. L0 writes its own copy in `tools/gen_l0_pack.py`.

The tables are found by structure (no address, no text, no build check): arrays of `{int32 id; const char* name}` records
(id at most 0xFFFF, name an identifier of at most 96 characters or null for an empty slot) back to back in a data section.
The states start at id 0; the message table starts again at 0 and numbers its rows by index to the end. The one run of
records that splits that way with at least 64 names on each side gives the 137 state slots (135 named; empty slots skipped)
and the 108 messages; the next smaller such run in the executable names 24 rows. Any other message count is refused. The engine
uses the October digital release's state numbering (including fixed ids such as the shark's attack state in `shark.h`).
Both CDs contain the August build with 134 named states and no entry for October state 64, an event state unused by
scripts. The importer increments CD ids from 64 onward and leaves slot 64 empty, preserving the engine's numbering
without supplying the missing name. Any other state count is refused.

## Verification

`convert.script_vocabulary_finds_tables`, `convert.script_vocabulary_refuses_other_message_counts` and
`convert.script_vocabulary_renumbers_august_states` check the table scan on a synthetic executable with our own names; `script_api.tables_are_sorted_and_unique` checks the API tables;
`data.script_vocabulary_of_installation` reads the user's executable and the converted pack. `convert.script_decode_ascii_only` checks the decoding and the refusal of non-ASCII bytes and of empty input.
`convert.script_refuses_bad_scenario_folders` checks that a folder without an id and a folder with two scripts stop the
conversion. `data.convert_scripts_of_installation` converts the installation (8 object and 24 world scripts), checks
each header line and encodes every written text back to the installed bytes. `script_pack.*` (`tests/unit/script_pack_test.cpp`)
checks the path mapping, the header refusal and the scenario ids; `data.script_pack_matches_installed_scripts` reads every
script back through the loader from a fresh conversion and from `run/packs/base` and compares it with the installed file.

Checked by hand on installed copies (read only, output outside the installation): `run/` (the test
installation), the extracted reference installation (same output, byte for byte), and the
`EURO Comp` group of the CD cabinets of the European CD (extracted with `obl-convert cabinet extract`): all 32
scripts are ASCII there too. The CD roots (with `data1.cab`) are not converted: like the GUI layouts, the command reads
extracted installations only. Every `LoadScript` target of the 24 world scripts is one of the 8 object scripts.
