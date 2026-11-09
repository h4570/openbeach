# Settings window

A player's start of `OpenBeach` first opens a small window with the settings that matter before playing, so nobody has to
edit an ini file. **Play** (or Enter) stores what changed and starts the game; **Quit**, Esc or closing the window starts
nothing (exit code 0). Code: `src/launcher/settings_window.cpp` (the window), `src/game/game/start_settings.cpp` (what it
reads and writes, unit test `tests/unit/start_settings_test.cpp`).

| Row | Stored in | Notes |
|---|---|---|
| Language | `BeachLife.ini` `[Language] Language` | only the languages whose table the selected pack has; shows the game's own fallback when the requested one is missing |
| Display | `openbeach.ini` `[Display] Windowed` | window or borderless fullscreen; Alt+Enter still switches in the game |
| Window size | `[Display] Scale` | 1x to 4x in steps of 0.5; off while Display is fullscreen |
| Keep mouse in window | `[Display] ConfineMouse` | |
| Frame rate cap | `[Display] MaxFps` | 30 to 240, or unlimited (0) |
| Intro movie | `[Intro] PlayMovie` | |
| Show this window | `[Launcher] ShowSettings` | off: the game starts at once; `--settings` shows the window once more |

A value from the file that the window does not offer (`Scale=1.75`) stays selectable. Only changed keys are written:
`openbeach.ini` is created from the same commented default file the game writes (`start_settings::kDefaultFile`), and
`BeachLife.ini` gets `[Beach Life Options] Version=1` when it has none, since the game reads nothing of the file without it
(the installer's `import.ps1` writes the language the same way). Environment overrides (`OBL_WINDOWED`, ...) are not shown.

## When it opens
In the launching process before anything else (on Windows the process that then starts the host; elsewhere the process
that becomes the game, which releases SDL video before the game initializes it again). It does not open:
- with `--no-settings`, `--hidden`, `--script`, `--headless` or `--no-entry`, or with `OBL_HIDDEN`, `OBL_SCRIPT` or
  `OBL_HEADLESS` set: test runs and tools never wait on it (`tools/capture.ps1` and the Linux and macOS smoke tests pass
  `--no-settings`);
- when the game directory has no `pack.json` for the selected pack (`OBL_PACK`): the game's own message says what to import;
- when `ShowSettings=0`, unless `--settings` is given.
The browser build has no such window (`browser_main.cpp`).

## Drawing
SDL's renderer and its built-in 8x8 font (`SDL_RenderDebugText`, ASCII only, so all text is English), no extra library.
The layout is 432x316 units, shown at a whole multiple (twice the desktop scale, smaller if it would not fit), letterboxed
when the system resizes it. Keyboard: Up/Down or Tab move, Left/Right or Space change a row, Enter plays, Esc quits; the
mouse picks a row, the left half of a value goes back and the rest forward, the wheel steps the focused row.
