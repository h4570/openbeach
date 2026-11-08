# Settings (`openbeach.ini`)

The game reads `openbeach.ini` from the game directory at start-up (`src/platform/config.cpp`) and creates it
with defaults and comments when it is missing. Environment variables override the file; scripted tests set them with
`# @env` headers. The game's own settings (resolution, volumes, dialog positions) are in `BeachLife.ini`.

| Key | Default | Env | Meaning |
|---|---|---|---|
| `[Display] Windowed` | 1 | `OBL_WINDOWED` | 1 = window, 0 = borderless fullscreen on the monitor; Alt+Enter switches and stores it (not in scripted runs) |
| `[Display] ConfineMouse` | 1 | `OBL_CONFINEMOUSE` | 1 = the pointer stays in the game window while it is active, so the screen edge scrolls the map (Alt+Tab or the in-game options let it go; never in hidden runs) |
| `[Display] Scale` | 1 | `OBL_SCALE` | window size multiplier 1-4 (decimals allowed), capped at the monitor size |
| `[Display] MaxFps` | 60 | `OBL_MAXFPS` | frame cap, 0 = unlimited (scripted runs: 0) |
| `[Intro] PlayMovie` | 1 | `OBL_PLAYMOVIE` | Eidos intro movie (scripted runs: 0) |

Other environment variables used by tools and tests: `OBL_HIDDEN` (hidden window), `OBL_SCRIPT` (scripted test run),
`OBL_HEADLESS` (headless replay run, [../architecture/headless/index.md](../architecture/headless/index.md)),
`OBL_WORLD_HASH_DIR` ([../testing/world-hashes.md](../testing/world-hashes.md)), `OBL_UPDATE_GOLDEN`, `OBL_KEEP_WORLD_HASH_ARTIFACTS`, `OBL_GAME_DIR` (CMake/unit tests),
`OBL_OGDATA` (unit tests: location of the user's installation), `OBL_PACK` (the pack id under `packs/`, default `base`; [../architecture/data-formats/maps-scenarios.md](../architecture/data-formats/maps-scenarios.md#the-l0-scenario)).

## Game language persistence

Game shutdown persists the requested language (`Settings::m_language`) to `BeachLife.ini` through `settings_store::Save`
and `settings::SaveMain` (`src/game/game/settings_store.cpp`). Pack text fallback changes only the text table's
language, and selecting `OBL_PACK=l0` does not change the requested setting. The L0 menu tests explicitly write
`Language.Language=0` through `@gameini`; `autotest.ps1` restores its saved INI in `finally`, but terminating the
harness can leave the temporary value behind.
Each native settings save now logs its INI path and `Language.Language` value for future writer attribution.

Both script harnesses set language 5 before each base test. Direct headless ctest launches read the existing INI.
The converter's `pack` command refuses an unimported requested language instead of silently recording a fallback
in `pack.json`; set the INI language or pass `--language` explicitly ([manifest contract](../architecture/data-formats/converter/pack.md)).

## Host
SDL supplies the window, keyboard, mouse, camera drag, audio and intro movie; SDL events feed the buffered Input object
contract. The window is always created by SDL; without a GPU renderer SDL's software renderer presents, and a presenter failure
drops frames while the game loop still runs on SDL events ([portable-host.md](../architecture/portable-host.md)).
