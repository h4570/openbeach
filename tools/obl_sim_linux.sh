#!/usr/bin/env bash
# Builds obl-sim for x86-64 Linux (docs/architecture/linux-host.md) with Clang: src/host_linux/CMakeLists.txt.
#   tools/obl_sim_linux.sh [-k] [extra cmake -D options...]
#   -k   keep going after compile errors (ninja -k 0), to see every failing source at once
# Prerequisites: cmake, ninja, clang.
# Build tree: build/linux-sim-x64/ (git-ignored), or $OBL_SIM_BUILD (a native Linux path builds faster from WSL than /mnt/c).
# Game directory the converter writes packs/ into: run/, or $OBL_GAME_DIR (a copy on the Linux file system).
# OBL_SIM_SKIP_CONVERSION=1 skips the pack conversion; OBL_SIM_ONLY=1 then also skips compiling obl-convert.
# OBL_SIM_BUILD_TYPE sets CMAKE_BUILD_TYPE (default Debug; the release build uses RelWithDebInfo, tools/ci/linux_release.sh).
set -euo pipefail

root="${OBL_SOURCE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
dir="${OBL_SIM_BUILD:-$root/build/linux-sim-x64}"
game="${OBL_GAME_DIR:-$root/run}"
keep=()
if [[ "${1:-}" == "-k" ]]; then keep=(-- -k 0); shift; fi

started=$SECONDS
cmake -S "$root/src/host_linux" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE="${OBL_SIM_BUILD_TYPE:-Debug}" \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    "$@" >/dev/null
echo "obl-sim configure: $((SECONDS - started)) s"
started=$SECONDS
convert=0
if [[ "${OBL_SIM_SKIP_CONVERSION:-0}" != "1" && -f "$game/BeachLife.exe" ]]; then convert=1; fi
# OBL_SIM_ONLY=1 (CI) builds only obl-sim when nothing is converted: obl-convert (-O2) is about 15 % of a cold compile,
# and CI's L0 step and a converted-pack cache hit never run it. The local gates keep compiling both.
targets=()
if [[ "${OBL_SIM_ONLY:-0}" == "1" && "$convert" == 0 ]]; then targets=(--target obl-sim); fi
# ${a[@]+...}: an empty array is "unbound" under set -u in bash before 4.4 (macOS ships 3.2).
cmake --build "$dir" ${targets[@]+"${targets[@]}"} ${keep[@]+"${keep[@]}"}
echo "obl-sim build: $((SECONDS - started)) s"
if [[ "$convert" == 1 ]]; then
    "$dir/bin/obl-convert" gui-tables "$game/BeachLife.exe" "$game/packs/base"
    "$dir/bin/obl-convert" game-parameters "$game" "$game/packs/base"
    "$dir/bin/obl-convert" scripts "$game" "$game/packs/base"
    "$dir/bin/obl-convert" maps "$game" "$game/packs/base"
    "$dir/bin/obl-convert" sounds "$game" "$game/packs/base"
    "$dir/bin/obl-convert" gui-layouts "$game" "$game/packs/base"
    "$dir/bin/obl-convert" images "$game" "$game/packs/base"
    "$dir/bin/obl-convert" sprites "$game" "$game/packs/base"
    "$dir/bin/obl-convert" media "$game" "$game/packs/base"
    for step in fonts catalog guests texts scenarios pack; do "$dir/bin/obl-convert" $step "$game" "$game/packs/base" >/dev/null; done
fi
if [[ -d "$root/data/packs/l0" && -d "$game" ]]; then
    mkdir -p "$game/packs/l0"
    cp -R "$root/data/packs/l0/." "$game/packs/l0/"
fi
echo "obl-sim:$dir/bin/obl-sim"
