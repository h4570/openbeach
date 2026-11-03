# Contributing to OpenBeach

Bug reports, fixes and new content are welcome. Use GitHub Issues to report a problem or to propose a larger change
before starting on it.

## Rule number one: no game data
Nothing from a retail copy of Beach Life may enter this repository: no graphics, sounds, music, video, texts, scenario
data, or files converted from them, and no references recorded on them. The same goes for screenshots of retail assets
in test references. Tests run on the project's own L0 pack ([L0 tests](docs/build/ci-l0.md)). If your bug only shows on
the imported game, describe it in the issue and attach a save, not the data.

## Building and testing
[Building](docs/build/building.md) covers Windows, Linux and WebAssembly. Before you open a pull request:

```powershell
./tools/runtests.ps1 -Pack public       # the game tests on the L0 pack; the first run also lays out run/
./build.ps1 -Test                       # unit tests, two of them start the game from run/
python tools/format.py --check          # clang-format 21
python tools/check_docs.py
python tools/check_doc_links.py
```

Run the tests your change affects (`./tools/runtests.ps1 -Filter <pattern>`) while you work. A new behavior gets a test
([game tests](docs/testing/game-tests.md), [unit tests](docs/testing/unit-tests.md)); time-dependent tests start with
`seed 1` and `fixed-dt 0.02` and advance with `step`. A bug fix has its own test, and a pull request that changes a
reference hash or image says why.

## Code and docs
- C++20, namespace `obl`, [code style](docs/workflow/code-style.md). Comments explain why, not what.
- The simulation core, `src/core/` and `src/common/` stay free of Windows APIs: CI runs on Linux and WebAssembly.
- The simulation is deterministic: use `src/game/game/rng.h` for random numbers, and no wall-clock time in game logic.
- Docs are in English and describe how things work now. A doc changes together with its code ([docs policy](docs/workflow/docs-policy.md)).

## Commits and pull requests
The project uses [Conventional Commits](https://www.conventionalcommits.org): `fix(game): stop guests queuing at a closed wharf`,
in lowercase and the imperative, at most 72 characters, without a trailing period. The body says why. The commits on `main`
compute the next version: `feat` raises the minor one, `fix` the patch. Types and scopes are in [CLAUDE.md](CLAUDE.md).
Keep a pull request to one change: it is squash-merged, so its title is what counts. A pull request with several changes is
merged with a merge commit instead, so give each change one clean commit; CI checks the title and every commit.
By contributing you agree that your work is licensed under GPL-3.0-or-later, the licence of this project.
