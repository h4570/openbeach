#!/usr/bin/env bash
# The WebAssembly checks (docs/architecture/browser-build.md): obl_unit_tests and the headless obl-sim built with
# Emscripten and run under Node. The CI wasm job and tools/linux_check.ps1 run it.
#   tools/wasm_linux.sh [extra cmake -D options...]
# Steps: the synthetic unit tests (-data.), the browser import module (importer/web/) with its Node checks, the browser
# game's build (compile only) and its pre-js under Node, the static site ($build/site, tools/web_site.sh) with its page
# under Node, the L0 scripts on the committed own pack; with a converted game directory
# ($OBL_WASM_GAME holding packs/base) also the installed-data oracles (data.) and the golden world hashes of S1-S12 plus
# the replays, which must equal the native x64 references.
# Emscripten: an activated emsdk (emcc on PATH) or $EMSDK / ~/emsdk, which is sourced. Build trees: $OBL_WASM_BUILD
# (default build/wasm) with unit/ and sim/ below it. Exit code: 1 when a step fails; every step runs.
set -uo pipefail

root="${OBL_SOURCE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
export OBL_SOURCE_ROOT="$root"
build="${OBL_WASM_BUILD:-$root/build/wasm}"
game="${OBL_WASM_GAME:-}"
status=0

if ! command -v emcc >/dev/null; then
    # emsdk_env.sh prints its PATH changes; only errors matter here.
    source "${EMSDK:-$HOME/emsdk}/emsdk_env.sh" >/dev/null 2>&1 || { echo "wasm_linux: no emsdk" >&2; exit 1; }
fi
echo "emcc $(emcc --version | sed -n '1s/.*) \([0-9.]*\).*/\1/p'), node $(node --version)"

step() {
    echo "== $1 =="
    shift
    "$@" || { echo "FAIL step exit $?"; status=1; }
}

# The unit tree is configured with the converted game directory when there is one (OBL_GAME_DIR is compiled in).
build_unit() {
    emcmake cmake -S "$root/tests/unit/linux" -B "$build/unit" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DOBL_GAME_DIR="${game:-$root/run}" "$@" >/dev/null && cmake --build "$build/unit"
}
build_sim() {
    emcmake cmake -S "$root/src/host_linux" -B "$build/sim" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@" >/dev/null &&
        cmake --build "$build/sim" --target obl-sim
}
# The browser import (importer/web/): obl-import-web.js/.wasm and import_worker.js in importer/bin/.
build_importer() {
    emcmake cmake -S "$root/importer/web" -B "$build/importer" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@" >/dev/null &&
        cmake --build "$build/importer"
}
# The browser game (docs/architecture/browser-build.md): the windowed OpenBeach.js/.wasm; compiled only, no browser here.
build_browser() {
    emcmake cmake -S "$root/src/host_linux" -B "$build/browser" -G Ninja -DCMAKE_BUILD_TYPE=Release -DOBL_LINUX_WINDOWED=ON         "$@" >/dev/null && cmake --build "$build/browser" --target OpenBeach
}
# Runs from the checkout root, where the unit tests find tests/ and data/; exit 77 means every selected test skipped.
unit_tests() {
    local code=0
    (cd "$root" && node "$build/unit/bin/obl_unit_tests.js" "$1") || code=$?
    [[ "$code" == 0 || ( "$code" == 77 && "$1" == data. ) ]]
}
# obl_sim_tests.py runs a host executable; this wrapper is that host for the Node build.
sim_tests() {
    python3 "$root/tools/obl_sim_tests.py" --sim "$build/obl-sim" "$@"
}

step "Build unit tests" build_unit "$@"
step "Synthetic unit tests" unit_tests -data.
step "Build importer web adapter" build_importer "$@"
step "Importer web adapter under Node" node "$root/importer/web/node_check.cjs" "$build/importer/bin/obl-import-web.js"
step "Build obl-sim" build_sim "$@"
step "Build browser game" build_browser "$@"
step "Browser game files under Node" node "$root/src/launcher/browser_files_check.cjs" "$root/src/launcher/browser_files.js"
step "Static site" bash "$root/tools/web_site.sh" "$build/browser/bin" "$build/importer/bin" "$build/site"
step "Site page under Node" node "$root/web/site_check.cjs" "$root/web/site.js" --site "$build/site"
printf '#!/bin/sh\nexec node "%s" "$@"\n' "$build/sim/bin/obl-sim.js" > "$build/obl-sim"
chmod +x "$build/obl-sim"

# The L0 game directory as tools/ci/l0_sim_tests.sh makes it: only the own pack, an empty Data/ and a settings file.
l0="$build/l0-run"
rm -rf "$l0"
mkdir -p "$l0/packs/l0" "$l0/Data"
cp -R "$root/data/packs/l0/." "$l0/packs/l0/"
printf '[Beach Life Options]\nVersion=1\n' > "$l0/BeachLife.ini"
step "L0 tests without game data" sim_tests --game-dir "$l0" \
    --filter l0_boot l0_gameplay l0_guests l0_goal l0_builders "l0_bot_*"

if [[ -n "$game" && -d "$game/packs/base" ]]; then
    step "Installed-data unit oracles" unit_tests data.
    step "S1-S12 world hashes and replays" sim_tests --game-dir "$game" --filter "*.golden" headless_sim "replay_*"
else
    echo "no converted game directory (OBL_WASM_GAME with packs/base): data steps skipped"
fi
exit "$status"
