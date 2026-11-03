# CLAUDE.md

Rules for coding agents in this repository (`AGENTS.md` points here). Details live in `docs/` (start at `docs/index.md`).

## Project
**OpenBeach** is an open-source engine in C++20 for the 2002 resort tycoon Beach Life. It plays on the files of the player's
own copy, which a converter reads once, and it ships a small scenario of its own, the L0 pack, that needs no original data.
The game builds for x64 Windows, Linux and WebAssembly; the browser site lives in `web/`.

## Rules
- **No game data in this repository:** no graphics, music, video, texts or scenario data of the original game, nothing
  extracted from them. Everything the tests need comes from the L0 pack. `python tools/check_content.py` checks files.
- **The world hashes and replays are the behavior contract.** A change that moves a reference is a bug, unless the same
  commit fixes a bug on purpose, has its own test and says why the reference moved.
- A bug fix has its own test. New tests use `boot-gameplay` + `await` + `expect`; time-dependent ones start with `seed 1`
  and `fixed-dt 0.02` and advance with `step`.
- `src/core/`, `src/common/` and the simulation core stay free of Windows APIs.
- Code style: `docs/workflow/code-style.md`. C++20, namespace `obl`, `PascalCase` functions and types, `g_` globals, `k`
  constants; comments explain why. Run `python tools/format.py --check` on what you touch.
- Docs are in English, topics stay short, and a doc moves with its code (`docs/workflow/docs-policy.md`).
- Runs must not disturb the machine: the game runs hidden (`--hidden`, `tools/capture.ps1`).

## Commits
[Conventional Commits](https://www.conventionalcommits.org): `type(scope): subject`, in lowercase and the imperative, at most
72 characters, no trailing period. The body says why.

- Types: `feat`, `fix`, `perf`, `refactor`, `test`, `docs`, `build`, `ci`, `chore`, `revert`.
- Scopes: `game`, `importer`, `site`, `platform`, `saves`, `tools`, or none.
- `feat` raises the minor version and `fix` the patch. `!` or `BREAKING CHANGE:` raises the major one, only when saves or an
  imported pack stop working. A refactor is never breaking.
- A pull request with one change is squash-merged, so its title is the commit on `main`. One with several changes is rebased
  onto `main` ("Rebase and merge"), so each of its commits reaches the changelog: give each change one clean commit (no
  "review fixes"). Never a merge commit: GitHub writes the title into it, and the changelog lists that change twice. `tools/check_commits.py` checks the title and every commit of a pull request in CI.

## Building and testing
```powershell
./build.ps1                         # x64 Debug, Windows
./tools/runtests.ps1 -Pack public   # game tests on the L0 pack; the first run lays out run/
./build.ps1 -Test                   # unit tests, two of them start the game from run/
```
Linux and WebAssembly: `docs/build/building.md`. Validate in proportion to the change: run the affected tests with
`./tools/runtests.ps1 -Filter <pattern>`. On a failure, search `run/testlogs/<test>.log` for `FAILED|CRASH|mismatch`.

## Layout
```
src/core/      portable helpers without game concepts
src/game/      game modules (src/game/<module>/)
src/platform/  Windows/SDL backends and host services
src/testing/   test interpreter, headless simulation
src/launcher/  OpenBeach, the host
importer/      converter from the player's copy, CLI and browser adapter
web/           browser site
tests/         game scripts + manifest, unit tests, tool tests, references
tools/         build, test and data scripts
data/packs/l0/ the L0 pack
```
