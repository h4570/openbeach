# L0 pack provenance

Every member of `data/packs/l0/` (233 files) is either produced by `tools/gen_l0_pack.py` or
hand-authored. `tests/tools/test_l0_pack_reproducible.py` regenerates the pack into a temporary directory and compares
every generated member byte for byte; it pins the remaining hand-authored members by SHA-256 and checks their exact set.

## Generated members (229)
Images and screens, sprites and sprite metadata, menu and play sprites, palette modifiers, fonts, maps and water masks,
the catalog (buildings, commodities, families, desires, opening hours, staff, vehicles), the text table, the scenario,
the world and object scripts, the object vocabulary, guests, `gui/in_game.gui`, `gui/credits.json`,
`gui/object_browser.json`, `gui/main_menu.gui`, `gui/intro.gui`, `rules/game_parameters.json`, sounds (synthesized PCM tones), and the silent `music/*.mp3` and `video/intro.mpg` (ffmpeg
from synthetic sources with metadata stripped). All content is written by code in the generator from numbers and shapes
chosen for L0; no game content input file is read (the message vocabulary reads our enum header). The test covers fonts, catalog, scripts, audio and media alike.

## Hand-authored members (4)
| Member | Origin |
|---|---|
| `pack.json` | Pack manifest written by hand when the L0 pack was installed (`01b00c477`); fields follow `obl.pack`. |
| `gui/layouts.json` | Maps layout names to the `.gui` files of this pack; names are our own screen identifiers. |
| `gui/game_won.gui` | Empty layout (header only). |
| `scenarios/campaign.json` | One-scenario campaign naming the own scenario `s1`. |

## Format vocabulary and pack choices
The startup files are authored as literals in `write_startup_content`; their geometry and parameter values are L0
choices. The following strings stay because the engine interprets them as part of its pack interface:

| Vocabulary | Meaning |
|---|---|
| `obl.game_parameters`, `version`, `parameters` | Parameter document schema. |
| `play_movie`, `sound_enabled`, `night_day_enabled`, `startup_in_windowed_mode`, `quick_load_map` | Startup switches. |
| `day_length_in_seconds`, `map_gridsize_xy`, `map_scale_z` | Clock and map units. |
| `buffer_lightmaps`, `dont_unload_map_chunks`, `refresh_rects` | Renderer settings. |
| `debugfont_filename`, `debugfont_name`, `debugfont_size` | Diagnostic font settings; the face and filename values are our `L0 Diagnostic` and `l0_diagnostic`. The renderer ignores those two strings. |
| `map_rmb_movement_speed`, `map_nudge_speed`, `map_max_velocity` | Camera settings. |
| `#obl-gui 1`, `Font`, `DialogItem`, `TGA`, `Button`, `RadioGroup`, `RadioButton`, `Variable`, `VariableImage`, `Text`, `ListBox` | GUI grammar; `TGA` uses logical `.tga` references resolved to pack PNG files. |
| `None` / `NONE` | Absent image, label or binding. |
| `GuiFont8`, `GuiFont8b`, `GuiFont12b` | Font slots shared with engine UI. |
| `MainMenu`, `ScenarioScreen`, `ExtrasScreen`, `CreditsScreen`, `LoadGameMenu`, `ResetProgressConfirm`, `DeepRedLogo`, `FrontScreen` | Dialog lookup names used by the menu and intro state machines. |
| `ShowScenSel`, `PlaySandBox`, `LoadGameMenu`, `ExtrasMenu`, `ExitWindows`, `ShowMainMenu`, `StartNewGame`, `CreditsMenu`, `ResetProgress`, `LoadGame`, `DeleteLoadGame`, `ExitLoadGame`, `ExitDialog`, `ResetProgressConfirm` | Button actions. |
| `GAME_SCENARIO_SELECT`, `SS_CUR_SCENARIO_TITLE`, `SS_CUR_SCENARIO_STATUS`, `SS_CUR_SCENARIO_DESC`, `SCENARIO_PIC_LARGE`, `SANDBOX_TITLE`, `SCENARIO_PIC_LOAD`, `LoadInfoBox`, `LoadListBox` | Radio, variable, picture and list bindings. |

Picture references in these layouts use `l0_welcome`, `l0_islands`, `l0_options`, `l0_acknowledgements`, `l0_opening`
and `l0_title` logical `.tga` references, resolved to identically named PNG files. Tooltip keys use `L0_MENU_TIP_*`; labels and sprites use the existing `L0_*` / `l0_*` names.
The renamed pictures keep the same diagnostic lettering and pixels: the drawing labels are frozen artwork, independent
of filenames. Engine-selected screen names `loading_1`, `loading_2` and `completion0` through `completion8` remain format vocabulary.
The menu tooltip keys remain absent from the text table, preserving their empty displayed text.
No engine change or reference hash update is needed for these pack choices.
