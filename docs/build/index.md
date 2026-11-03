# Build and run

| Topic | Summary |
|---|---|
| [building.md](building.md) | Requirements (Windows, Visual Studio 2026, x64), `build.ps1` configurations and outputs, what each binary is, and the clang-format tool. |
| [game-data.md](game-data.md) | The game data comes from the user's own copy: the working directory `run/` and the converted pack `run/packs/base/` (location, producers, layers, no fallback). Includes the rule that game data never enters git. |
| [running-the-game.md](running-the-game.md) | Starting the game (`OpenBeach.exe`), hidden runs that do not disturb the desktop, logs, killing leftovers safely, and the start-up sequence. |
| [ci.md](ci.md) | The public GitHub Actions workflow: triggers, jobs and their local equivalents. |
| [ci-local.md](ci-local.md) | Reproducing the CI jobs on a local Linux machine or WSL. |
| [releases.md](releases.md) | The release archives for Windows and Linux: layout, README and AGENTS.md, import scripts, optimized build, glibc floor, smoke tests, testing by hand. |
| [ci-l0.md](ci-l0.md) | No-data L0 CI tests, compile-time diagnosis, compiler-cache settings and fast-build measurements. |
| [settings.md](settings.md) | `openbeach.ini` keys and their environment-variable overrides. |
