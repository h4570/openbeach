# Converter: gui runtime

## GUI writer and runtime handoff

`obl-convert gui-tables <BeachLife.exe> <output directory>` now writes `gui/object_browser.json` (`obl.object_browser`,
version 2) and `gui/credits.json` (`obl.credits`, version 2). Without the output argument it still prints the discovered
records. `importer/src/writers/gui_writer.{h,cpp}` consumes `FindGuiTables`, marks both documents `provenance: converted`, and
validates both before writing. No converted output belongs in git. The two files are written separately; an I/O failure
can leave a partial pair, so discard or regenerate that output before using it.

`src/formats/gui_content.{h,cpp}` is the portable, owning reader. It validates through `json_schema::Registry` using
schemas embedded at configure time (`cmake/gui_content.cmake`, shared by Windows and both Linux entry points), then
checks engine semantics. Guest/building entries and headings name stable filter kinds; the engine adapter receives
numeric ids. Other lists retain signed values (including cell masks). Separators preserve order. Exactly eight row
button resources are positional. Credits preserve every extracted row, with only the final row carrying kind 1; the
reader assumes no fixed count. Parse failure preserves the caller's previous content.

Runtime: `src/game/ui/gui_content_runtime.{h,cpp}` loads both files from `packs/base/gui/` (`LoadPack`) in `InitGame` and keeps
the string storage alive. Missing or invalid files only log; the first GUI access then stops a windowed game with
`FatalError` (headless runs and `standalone_no_exe` never touch the GUI). The object browser lists, heading lookups
(`gui_tables.h`), row button resources and the credits roll read it; heading and credit loops use
the loaded sizes. The files are written by `obl-convert` ([game-data.md](../../../build/game-data.md#converted-pack-in-runpacksbase)).

Engine-owned values are our code: filter value cells (`kFilterDefinitions`) and the fixed names (`"None"`,
`"NONE"`, `ButtonClick`, `%d`, the radar/progress toggle actions) in `src/game/ui/gui_definitions.h`, the GUI action
bindings in `gui_controls.cpp`, the four base resolutions in `game/ui/resolutions.cpp`.

OpenBeach's own credits (`src/game/game/own_credits.{h,cpp}`) follow the final converted group and take over its
end-of-roll marker. Their lines start at 9000 (`CREDIT_ENTRY_9000`...). Built-in texts follow the game's language
(en, pl, de, fr, it, es) and use the same renderer byte encoding as imported texts. A language file defining the same
key wins. Order, fonts, colors and spacing are independent of the language. The five `credits_<language>.txt`
hidden-game tests select the language through `@gameini`; `call-drive ui.text-table-find-key` checks every own
row's loaded value, including encoding and pack overrides. Unit tests cover translations and the Polish name's bytes.

Coverage: `gui_content.*` uses invented records for round trips, separators, signed masks, validation failures,
unchanged output on invalid input and filesystem errors. Existing optional `data.convert_gui_tables_*` tests also
compare imported tables from the digital release and available CDs with the current frozen metadata.
