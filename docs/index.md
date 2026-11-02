# OpenBeach documentation

Start here. Every directory below has its own `index.md` that summarizes each topic file in two or three sentences, so
read the index of the category you need and then open only the topic you need. Rules for writing docs:
[workflow/docs-policy.md](workflow/docs-policy.md).

| Category | What it covers |
|---|---|
| [workflow/](workflow/index.md) | Documentation policy and C++ style for contributors. |
| [build/](build/index.md) | Building the project, preparing the game data in `run/`, running the game and its settings file. |
| [architecture/](architecture/index.md) | How the game is structured: simulation, GUI and host boundaries, starting with the command layer (input as commands, recording and replay). |
| [testing/](testing/index.md) | Unit tests, scripted game tests and the test runner, determinism, world hashes and golden images. |
| [specs/](specs/index.md) | Behavior specs per module family, written from tests and observed behavior (world managers, guests, staff and moving objects so far). |

Project overview and getting started: [README.md](../README.md).
