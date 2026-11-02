# Continuous integration (GitHub Actions, Linux)

The public workflow is `.github/workflows/ci.yml`. It
runs on pushes and pull requests to `main`, and by hand (`workflow_dispatch`). It needs no game data and no secrets: every
check runs on the project's own L0 pack. A newer push or pull-request update cancels the older run on the same branch.

## Jobs
| Job | What it runs | Local equivalent |
|---|---|---|
| `lint` | `tools/format.py --check` (clang-format 21.1.2 from pip), `tools/check_docs.py`, `tools/check_doc_links.py`, `tools/check_content.py`, `tools/check_layers.py`, then the Python tool tests (`tests/tools/`, `tools/tests/`) | the same commands (needs `pip install clang-format==21.1.2 pillow`) |
| `unit (gcc\|clang)` | `obl_unit_tests` built by `tools/unit_linux.sh` with `-DOBL_UNIT_RELOCATABLE=ON` and `OBL_UNIT_SKIP_DATA=1`: synthetic cases only | `OBL_UNIT_SKIP_DATA=1 bash tools/unit_linux.sh gcc\|clang` |
| `sim` | `tools/ci/l0_sim_tests.sh`: builds the headless `obl-sim` and runs the L0 scripts, replays and golden hashes of `tests/manifest.txt` (the `@l0` entries); the test logs are uploaded on failure as `obl-sim-testlogs` | `bash tools/ci/l0_sim_tests.sh` |
| `wasm` | `tools/wasm_linux.sh`: unit tests and `obl-sim` built with Emscripten 6.0.13 (emsdk cached per version) and run under Node; the browser game and the importer, assembled into the static site and uploaded as the artifact `browser-site` (14 days; [browser-build.md](../architecture/browser-build.md#site)) | `bash tools/wasm_linux.sh` with `EMSDK` set |
| `deploy` | only by hand with the input `deploy` on `main`, after every job above: publishes the site to GitHub Pages | `gh workflow run ci.yml --ref main -f deploy=true` |

The release workflow starts that run after it makes a release, so a merged release pull request publishes the site. The run
replaces the push run of the same commit; a newer push would cancel it too, and the site then needs the run by hand.

The unit, `sim` and `wasm` jobs compile through ccache, kept in the Actions cache per job and compiler. Package installs
skip recommended packages. The windowed game and anything that needs the player's own copy of the game are not part of
CI; they run locally ([running-the-game.md](running-the-game.md)).

## Related topics
- [Local checks](ci-local.md): reproducing the jobs on your machine.
- [L0 tests and compile time](ci-l0.md): no-data coverage, compiler-cache diagnosis and fast builds.
