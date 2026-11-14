# Converter: GUI layouts

`obl-convert gui-layouts <installation directory> <output directory> [--schemas <dir>]` writes `gui/<name>.gui` for every
installed GUI script of `Data/Script/GUI/` that belongs to a layout below, and `gui/layouts.json` (`obl.gui_layouts`,
[game-data.md](../game-data.md#gui-layouts)). The scripts are `DialogItem`, `Button` and `TGA` descriptions, not text
tables: they are kept as text in their own syntax, decoded.

## Layout choices

The converter routes the files by their installed names, not by a list of files: `<base>.ble` is a layout's `*`
fallback and `<base>_<abbreviation>.ble` its variant for the language whose text table word starts with the abbreviation
(`fre` -> `text_french.ble` -> `fr`, `Polish` -> `pl`; at least three letters, any case; `LanguageOfAbbreviation` in
`text_convert.cpp`). A name with an unknown abbreviation is no layout. Our own role table in `gui_layout_convert.cpp` names
each layout's base name and output stem; only the reset screen narrows the languages (the engine's reset screen takes the
main menu's Polish variant and the fallback otherwise). For the supported installations this gives:

| Layout | Language | Installed script | Output |
|---|---|---|---|
| `in_game` | `pl` | `GUI_Polish.ble` | `in_game_pl.gui` |
| `in_game` | `*` | `GUI.ble` | `in_game.gui` |
| `intro` | `*` | `Intro.ble` | `intro.gui` |
| `main_menu` | `fr`, `it`, `de`, `es`, `pl` | `MainMenu_fre`, `_ita`, `_ger`, `_spa`, `_pol` `.ble` | `main_menu_{fr,it,de,es,pl}.gui` |
| `main_menu` | `*` | `MainMenu.ble` | `main_menu.gui` |
| `reset_menu` | `pl` | `MainMenu_pol.ble` | `main_menu_pl.gui` (same file) |
| `reset_menu` | `*` | `MainMenu.ble` | `main_menu.gui` (same file) |
| `game_won` | `pl` | `gamewon_pol.ble` | `game_won_pl.gui` |
| `game_won` | `*` | `gamewon.ble` | `game_won.gui` |

Sources: `game_states.h` (`GameWonGuiScript`, `ResetGuiScript`, `MainMenuGuiScript`), `world_test_state.cpp` (the
in-game GUI, Polish = language 5) and `IntroGuiScript`. `reset_menu` differs from `main_menu` only for the four
non-Polish languages, where the engine's reset screen always uses `MainMenu.ble`. The installation has no
`exitgame` script; the exit screen has no layout to convert.

A layout whose `*` fallback is absent from the revision is dropped (no guessing). A file absent from the revision is
skipped. Several choices may name one file; it is written once.

## Conversion contract

- Each byte is XOR-ed with 0xFF, then decoded through the language's code page: Windows-1250 for the Polish scripts
  (`*_pol.ble`, `GUI_Polish.ble`), Windows-1252 otherwise ([code_pages.h](../../../../src/core/code_pages.h)). Bytes the
  code page leaves undefined keep their C1 code point, as in the texts.
- Nothing is trimmed or normalized: CRLF line ends, trailing bytes and odd characters stay. An empty script is refused.
- The output is UTF-8 with the first line `#obl-gui 1` (the readers check it); the rest is the decoded script.
- `gui/layouts.json` is validated against `obl.gui_layouts` before it is written. Its `fonts` section is not written:
  the scripts name their fonts themselves (`Font(GuiFontPol8)`), and the photo font choice is open
  ([fonts.md](fonts.md)).
- Tier L1: the headless host builds the GUI from these layouts and bots read its dialogs.

## Engine reader

`gui_layouts::LoadLayouts` (`game/ui/gui_layouts.cpp`) reads and validates `gui/layouts.json` at start-up
(`game/ui/gui_layouts_runtime.cpp`). The states pass their layout key (`game/game/game_states.h`: `main_menu`, `reset_menu`,
`game_won`, `intro`, `in_game`) and the language number; `Resolve` takes the language's file or the `*` fallback. The
`LoadScript` path then asks for the file's text: `ReadText` drops the `#obl-gui 1` line and encodes the UTF-8 back into the
file's code page (`core/code_pages.cpp`: Windows-1250 for the `pl` choice, Windows-1252 otherwise). That is the byte the
source gave the parser after its XOR 0xFF, so the parser runs unchanged. A null layout (the exit screen, LoadGame) gives an
empty script, as the source did for a missing file. A layout or file the pack lacks stops the game with the reason.

After the read, `gui_manager_frame.cpp`'s `ReadLayout` passes the text through `own_controls::Apply` (`game/ui/own_controls.{h,cpp}`),
which adds OpenBeach's own controls to the original dialogs: today two sliders (`CURSOR_SPEED` at y 163-178 and `SCROLL_SPEED` at y 178-193;
texts `OBL_CURSOR_SPEED`, `OBL_SCROLL_SPEED` and their tooltips from `game/game/own_texts.cpp`; stored as `[Video] CursorSpeed` and
`ScrollSpeed`, 0-100) into the
Video tab of `InGameOptions`, below the checkbox that holds `GAME_NUDGE`. It does so only when the host's pointer is a thumb stick
(`own_controls::SetPadPointer`, the handheld build, [handheld.md](../../../build/handheld.md)); the converter and the files stay as the
installation had them.

The code page tables moved from the converter to `core/code_pages.*` (`CodePoint`, `ByteOf`, `Encode`) for this reader.

## Verification

`convert.gui_layout_decode_code_pages` checks the code-page mapping and the undefined byte on invented bytes.
`convert.gui_layout_refuses_without_fallback` checks that a revision without fallbacks writes nothing.
`data.convert_gui_layouts_of_installation` converts the installation, checks `layouts.json` (keys, fallbacks, file names)
and, for every installed script, encodes the written text back to bytes and compares it with the installed file.
`gui_layouts.resolve_follows_language_with_fallback`, `gui_layouts.read_text_encodes_the_code_page` and
`gui_layouts.load_refuses_a_layout_without_fallback` check the engine reader on invented files, and
`data.gui_layouts_match_engine_loader` checks that the text the parser gets for every installed script equals the
installed file after its XOR 0xFF.
