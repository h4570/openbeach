# Spec: language text table

## Purpose
Turns a language file (a list of quoted key and value strings) into a table the game looks texts up in by key, ignoring
case. The game's own texts can be added behind the file's, so a language file can override them. The language comes
from the settings; when its table was not imported, another imported one is used.

## Vocabulary
- **Entry**: a key (plain text) and a value (UTF-16 text). **Table**: the entries in file order, a slot array of the table's
  modulus (each slot holds an entry index or is empty), the count of entries placed in slots (`hashed`), the modulus and the
  path of the pack file the pairs came from.
- **Walk**: the scan of the file's units: strings are the text between a pair of double quotes, and the strings alternate
  key, value, key, value. The first unit (the byte-order mark in real files) is never inspected.
- **Own texts**: entries added by the game itself. The most entries a table holds is 4500 (`kMaxPairs`); the modulus used for
  the shipped files is 6151.

## Rules
1. The walk pairs the quoted strings in order into entries; text outside quotes (spaces, tabs, line breaks, comment text
   without quotes) is ignored (`text_table.alternates_keys_and_values`).
2. The walk knows nothing about comments: a quoted word inside a comment becomes a key and shifts the pairing of the
   following strings (`text_table.quotes_in_comments_take_part`).
3. A key without a value at the end of the file is dropped; so is an entry whose string has no closing quote; an empty file
   has no entries (`text_table.trailing_key_and_unterminated_quote`).
4. A quote in the very first unit is never seen: a file without the leading mark pairs one quote late, so the gap between two
   strings becomes a key (`text_table.trailing_key_and_unterminated_quote`).
5. The hash of a key is `h = h * 5 + c` over its characters as signed characters, starting at 0 (`"AB"` gives 391, an empty key
   0, the byte 0xff gives 0xffffffff) (`text_table.hash_and_probing`).
6. An entry goes into the slot of its hash modulo the table's modulus, or into the next free slot after it (wrapping around
   the end); when every slot is taken the entry stays in the entry list but gets no slot, so `hashed` is lower than the
   entry count (`text_table.hash_and_probing`: four equal keys in three slots give slots 0, 1, 2 in order and `hashed` 3;
   `text_table.make_table_places_entries_by_hash_slot`: a duplicate key takes the next free slot).
7. Own texts are appended only for keys the table does not have yet, compared ignoring case; a key from the language file
   keeps its own value. Appending stops at the entry limit it is given: with a limit of 2 a third text is not added
   (`text_table.append_adds_only_missing_keys`, `text_table.own_texts_append_only_missing_keys`).
8. Finding a key's entry index ignores ASCII case, returns the first of equal keys, and returns 0xffffffff for an empty
   key or a key that is not there (`text_table.find_key_ignores_ascii_case`).
9. Looking a text up by key probes from the key's hash slot past slots taken by other keys and around the end of the slot
   array, compares ignoring case, and returns the caller's fallback for an unknown key or a null key; it ends even when
   every slot is taken (`text_table.lookup_probes_past_collisions_and_wraps`).
10. A table made from no entries is valid and returns the fallback for every key; so does a table with modulus 0
    (`text_table.make_table_handles_no_entries_and_pair_limit`).
11. Making a table keeps the path, the modulus, one slot per modulus value, all entries in file order and the placed
    count (`text_table.make_table_places_entries_by_hash_slot`).
12. A table holds at most 4500 entries: of 4550 file entries and one own text, 4500 are kept
    (`text_table.make_table_handles_no_entries_and_pair_limit`).
13. The shipped language files, read from the player's own copy, give 4339 entries (French, German, Italian, Spanish) or 4342
    (Polish), all placed in slots of the 6151-slot table; the first key is `STRING_IDENTIFIER` and the last
    `MAINSS_SANDBOX12_DESC` (`data.text_table_parses_shipped_languages`, `data.text_table_native_construct_matches_parser`).
    These tests skip when the game data is not prepared.

### Language packs
14. The settings' language number picks a pack file: 0 en, 1 fr, 2 it, 3 de, 4 es, 5 pl, any other number en; the file is
    `text/<code>.json` (`text_load.language_codes_follow_the_settings_order`).
15. Loading a pack reads its key and value pairs in file order; a missing file loads with no entries and no error; a file that
    does not follow the text pack format fails, names the file in the error and leaves no entries
    (`text_load.pack_table_reads_entries_and_refuses_bad_files`).
16. When the selected language's table is available (in the pack or a mod) it is used. Otherwise the first available language
    in the settings order is used, and the first time only a notice names the requested and the used language; an imported
    table that does not load still counts as available, and its load reports its own error. With no table at all the
    selection fails with an error that names the pack (`text_load.missing_language_falls_back_and_notifies_once`).
17. A fallback changes only the table's language: the language saved in the settings stays the requested one
    (`text_load.l0_fallback_keeps_requested_language`).
18. The text the engine draws is turned into the code page of the language: Polish uses Windows-1250, the others
    Windows-1252; ASCII, Latin-1 units and units the code page has no byte for pass unchanged
    (`text_load.engine_value_returns_code_page_bytes`).

## Edge cases
- Keys that collide in a small table (modulus 3) wrap around the slot array; the last slot continues at slot 0
  (`text_table.hash_and_probing`, `text_table.lookup_probes_past_collisions_and_wraps`).

## Not covered by tests
- Decoding of the file bytes into units (the tests invert the bytes of a written file themselves and hand the units over).
- What the game shows for a missing key; the table only returns the caller's fallback.

## Tests
`tests/unit/text_table_test.cpp` (the two `data.` tests need the prepared `run/` directory),
`tests/unit/text_load_test.cpp`.
