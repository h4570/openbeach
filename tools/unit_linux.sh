#!/usr/bin/env bash
# obl_unit_tests on Linux (docs/testing/unit-tests.md, "Linux"), for anyone without Windows.
#   tools/unit_linux.sh [gcc|clang|all|gcc-x64|clang-x64] [extra cmake -D options...]
# gcc/clang/all build x86-64 code (build/linux-<compiler>-m64/; the WebAssembly job covers 32-bit
# pointers). The -x64 modes build the portable part (docs/build/building.md):
# the root CMakeLists.txt with its 64-bit configure (cmake/portable_only.cmake), ctest unit_portable[_data].
# Prerequisites: cmake, ninja, gcc/g++ or clang/clang++.
# Build trees: build/linux-<compiler>-m64/ (git-ignored). Exit code: 1 when any configuration fails; "all" runs both.
# OBL_UNIT_SKIP_DATA=1 selects synthetic tests; OBL_UNIT_DATA_ONLY=1 selects installed-data oracles.
# OBL_GAME_DIR overrides run/ (passed at every configure, including reused build trees).
set -euo pipefail

root="${OBL_SOURCE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
which="${1:-all}"
shift || true

run_one() {
    local cc="$1" cxx="$2" dir="${OBL_UNIT_BUILD_ROOT:-$root/build}/linux-$1-m64"
    shift 2
    echo "== $cc =="
    local game="${OBL_GAME_DIR:-$root/run}"
    cmake -S "$root/tests/unit/linux" -B "$dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
        -DOBL_GAME_DIR="$game" "$@" >/dev/null
    cmake --build "$dir"
    local tests=()
    if [[ "${OBL_UNIT_DATA_ONLY:-0}" == "1" ]]; then
        tests=(-R '^unit_data$')
    elif [[ "${OBL_UNIT_SKIP_DATA:-0}" == "1" ]]; then
        tests=(-R '^unit$')
    fi
    ctest --test-dir "$dir" --output-on-failure "${tests[@]}"
}

# The 64-bit portable configure: the root project, which builds only cmake/portable_only.cmake when pointers are 8 bytes.
run_x64() {
    local cc="$1" cxx="$2" dir="${OBL_UNIT_BUILD_ROOT:-$root/build}/linux-$1-x64"
    shift 2
    echo "== $cc x64 =="
    local game="${OBL_GAME_DIR:-$root/run}"
    cmake -S "$root" -B "$dir" -G Ninja         -DCMAKE_BUILD_TYPE=RelWithDebInfo         -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" -DOBL_GAME_DIR="$game" "$@" >/dev/null
    cmake --build "$dir"
    local tests=(-R '^unit_portable(_data)?$')
    if [[ "${OBL_UNIT_DATA_ONLY:-0}" == "1" ]]; then
        tests=(-R '^unit_portable_data$')
    elif [[ "${OBL_UNIT_SKIP_DATA:-0}" == "1" ]]; then
        tests=(-R '^unit_portable$')
    fi
    ctest --test-dir "$dir" --output-on-failure "${tests[@]}"
}

case "$which" in
    gcc) run_one gcc g++ "$@" ;;
    clang) run_one clang clang++ "$@" ;;
    # A subshell keeps set -e inside one configuration, so a failing GCC run still lets Clang run.
    all) status=0; (run_one gcc g++ "$@") || status=1; (run_one clang clang++ "$@") || status=1; exit "$status" ;;
    gcc-x64) run_x64 gcc g++ "$@" ;;
    clang-x64) run_x64 clang clang++ "$@" ;;
    *) echo "usage: $0 [gcc|clang|all|gcc-x64|clang-x64] [-DOPTION=value...]" >&2; exit 2 ;;
esac
