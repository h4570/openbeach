# Local checks

Every job of the public workflow ([ci.md](ci.md)) can be reproduced on a Linux machine (or WSL Ubuntu on Windows) with the
same scripts. Install `cmake`, `ninja-build`, `zstd`, GCC and Clang as listed in [unit-tests.md](../testing/unit-tests.md);
`ccache` makes reruns much faster.

```bash
pip install clang-format==21.1.2 pillow
python tools/format.py --check && python tools/check_docs.py && python tools/check_doc_links.py
python tools/check_content.py && python tools/check_layers.py
python -m unittest discover -s tools/tests
OBL_UNIT_SKIP_DATA=1 bash tools/unit_linux.sh clang      # unit tests (also: gcc)
bash tools/ci/l0_sim_tests.sh                            # headless L0 scripts, replays, golden hashes
```

Run the shell scripts from a checkout with LF line endings (`git config core.autocrlf false`). Under WSL keep the
checkout and the build trees on the Linux file system; builds on `/mnt/c` are slow. The WebAssembly job needs an
Emscripten SDK (`EMSDK` points at it): `bash tools/wasm_linux.sh`.

The windowed game and the checks that need the player's own copy of the game are described in
[running-the-game.md](running-the-game.md) and [game-data.md](game-data.md).
