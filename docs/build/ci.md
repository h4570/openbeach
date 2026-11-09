# Continuous integration (GitHub Actions, Linux)

The public workflow is `.github/workflows/ci.yml`. It
runs on pushes and pull requests to `main`, and by hand (`workflow_dispatch`). It needs no game data and no secrets: every
check runs on the project's own L0 pack. A newer push or pull-request update cancels the older run on the same branch.

## Jobs
| Job | What it runs | Local equivalent |
|---|---|---|
| `lint` | `tools/format.py --check` (clang-format 21.1.2 from pip), `tools/check_docs.py`, `tools/check_doc_links.py`, `tools/check_content.py`, `tools/check_layers.py`, `tools/check_commits.py` (the title and every commit of a pull request, the last commit of a push), then the Python tool tests (`tests/tools/`, `tools/tests/`) | the same commands (needs `pip install clang-format==21.1.2 pillow`) |
| `unit (gcc\|clang)` | `obl_unit_tests` built by `tools/unit_linux.sh` with `-DOBL_UNIT_RELOCATABLE=ON` and `OBL_UNIT_SKIP_DATA=1`: synthetic cases only | `OBL_UNIT_SKIP_DATA=1 bash tools/unit_linux.sh gcc\|clang` |
| `sim` | `tools/ci/l0_sim_tests.sh`: builds the headless `obl-sim` and runs the L0 scripts, replays and golden hashes of `tests/manifest.txt` (the `@l0` entries); the test logs are uploaded on failure as `obl-sim-testlogs` | `bash tools/ci/l0_sim_tests.sh` |
| `wasm` | `tools/wasm_linux.sh`: unit tests and `obl-sim` built with Emscripten 6.0.13 (emsdk cached per version) and run under Node; the browser game and the importer, assembled into the static site and uploaded as the artifact `browser-site` (14 days; [browser-build.md](../architecture/browser-build.md#site)) | `bash tools/wasm_linux.sh` with `EMSDK` set |
| `deploy` | only by hand with the input `deploy` on `main`, after every job above: publishes the site to GitHub Pages | `gh workflow run ci.yml --ref main -f deploy=true` |

## Release workflow
`.github/workflows/release.yml` runs release-please on every push to `main`: the `release` job keeps the release pull
request open, and merging it makes the tag and the GitHub release. Events of `github.token` start no other workflow, so
whatever a release needs runs in this file, after `release` and on its outputs `release_created` and `tag_name`:
- the site: `release` starts the CI run with `deploy` (below);
- `linux`: builds the Linux tarball on Ubuntu 22.04, runs the L0 references on the optimized build, smoke-tests it under
  Xvfb (also in `debian:12` and `ubuntu:24.04` containers) and attaches it to the release ([releases.md](releases.md));
- `windows`: builds the Windows zip and installer on `windows-2025`, runs the same L0 references, starts the packed game
  and the silently installed one hidden and attaches both.

By hand (`workflow_dispatch`) only `linux` and `windows` run and keep the archives as the workflow artifacts
`linux-release` and `windows-release`.

The CI run started after a release replaces the push run of the same commit, so a merged release pull request publishes
the site; a newer push would cancel it too, and the site then needs the run by hand.

The stargazers workflow (`.github/workflows/stargazers.yml`) runs `tools/update_stargazers.py` when someone stars the repository and once a day.
It draws the list as an SVG and pushes it to the `stargazers-data` branch, which no ruleset protects; the README embeds the file
from there. So `main` gets no commit per star and its pull request rule stays whole.

The unit, `sim` and `wasm` jobs compile through ccache, kept in the Actions cache per job and compiler. Package installs
skip recommended packages. The Windows game and anything that needs the player's own copy of the game are not part of
CI; they run locally ([running-the-game.md](running-the-game.md)). The windowed Linux game is built and smoke-tested by the
release workflow.

## Related topics
- [Local checks](ci-local.md): reproducing the jobs on your machine.
- [L0 tests and compile time](ci-l0.md): no-data coverage, compiler-cache diagnosis and fast builds.
