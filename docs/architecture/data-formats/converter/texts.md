# Converter: texts

`obl-convert texts <installation directory> <output directory> [--schemas <dir>]` writes `text/<lang>.json`
([obl.text](../assets.md#texts)) for every installed text table of `Data/`:

| Installed file | Language |
|---|---|
| `text_uk.ble` | `en` |
| `text_french.ble` | `fr` |
| `text_italian.ble` | `it` |
| `text_german.ble` | `de` |
| `text_spanish.ble` | `es` |
| `text_polish.ble` | `pl` |

A revision may lack some of them ([sources.md](../sources.md)): a missing table is skipped, and an installation with none
is an error. The GUI scripts of `Data/Script/GUI` (`gui_polish`, `gamewon_pol`, `mainmenu_*`) are not text tables: they are
GUI layouts ([game-data.md](../game-data.md#gui-layouts)) ([gui-layouts.md](gui-layouts.md)). The photo fonts
(`Data/Fonts/photo16`, `photopol16`) are glyph fonts, converted by `obl-convert fonts` ([fonts.md](fonts.md)).

## Conversion contract

- The file is UTF-16LE with every byte XOR 0xFF. An empty or odd-sized file is refused.
- The pairs are read with the engine's walk (`text::Parse`, [src/game/tables/text_table.cpp](../../../../src/game/tables/text_table.cpp)):
  a quote after any unit opens a string that ends at the next quote, or at the end of the file; strings alternate key and
  value, so quotes in comments take part; a trailing key without a value is dropped. Duplicates stay, in file order.
- Keys must be ASCII; a non-ASCII key is refused rather than narrowed.
- At most 4500 pairs are read, the capacity of the current reader. The shipped Polish table holds 4342.
- A value stops at its first NUL unit. Units up to 0xFF are code-page bytes: Windows-1250 for `pl`, Windows-1252 for
  the others ([code_pages.h](../../../../src/core/code_pages.h)). A byte the code page leaves undefined keeps its
  C1 code point (U+0080..U+009F), so no character is lost. Higher units pass through unchanged.
- A value that ends up with an unpaired surrogate is refused: the converter does not replace it with U+FFFD.
- The document is validated against `obl.text` before the sink receives it; the JSON is UTF-8 and written with two-space
  indentation.

Tier L1 (text lands in saves and bot-visible dialogs, [assets.md](../assets.md#texts)). Converted content stays outside git.

The engine reads the pack's `text/<lang>.json` instead of the installed table (`text_load::LoadPack`, [src/game/tables/text_load.h](../../../../src/game/tables/text_load.h)):
the values go back to the code-page units the engine draws (`text_load::EngineValue`, the inverse of the mapping above;
the Polish table's 17 units above 0xFF, which the source indexed past its 256-entry glyph table, fold to their bytes),
so the table holds what the source's walk built. At startup, a missing selected language falls back to the first imported
table in settings order (en, fr, it, de, es, pl), including mod overrides. A notice explains once per launch that the language
was not imported from the player's copy. Only the text table uses the fallback; Settings and BeachLife.ini keep the requested language.
GUI layouts keep using the requested language and their own layout fallback.
No available tables or an invalid selected file stops the game with a diagnostic.

## Verification

`text_load.missing_language_falls_back_and_notifies_once` covers an English setting with only Polish imported,
loaded fallback text, one notice, an available selection, mod overrides and no imported languages.

`convert.text_walk_matches_engine_parse` and `convert.text_walk_nul_and_unterminated` run invented files through the
converter's walk and the engine's `text::Parse`, comparing counts, order, keys and raw values, including a comment quote,
a duplicate key, a dropped trailing key, an embedded NUL and an unterminated value.
`convert.text_code_page_values` and `convert.text_rejects_bad_input` check the code-page mapping (0xB9 is U+0105 in
Polish and U+00B9 in Western, 0x92 is U+2019, undefined bytes, surrogate pairs) and the refusals.
`data.convert_texts_of_installation` compares every installed table with the engine's own table of the same bytes after
the same code-page mapping, and skips without an installation.
