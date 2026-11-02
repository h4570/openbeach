#!/usr/bin/env bash
# Line coverage of src/common/ by obl_unit_tests on Linux (docs/testing/unit-tests.md).
#   tools/unit_coverage.sh [-DOPTION=value...] [-- <test name prefixes, -prefix excludes>]
# Same prerequisites as tools/unit_linux.sh (GCC only: gcov ships with it, gcovr is not needed). The build tree and the
# report live in build/linux-cov-gcc/ (git-ignored): coverage.txt (per file, lowest coverage first) and coverage.json.
#
# Only C++ is instrumented (--coverage in CMAKE_CXX_FLAGS), so SDL3 (C) stays out of the counters. A line counts as
# covered when any translation unit executed it (a header included by many tests is merged across them). Files under
# src/common/ that no unit includes are listed first as "n/c"; included files without executable lines (declarations,
# layouts) last as "-".
# Exit code: the test program's (0 = all passed, 77 = all skipped), or 1 when the build fails.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dir="$root/build/linux-cov-gcc"
cmake_args=()
test_args=()
while [[ $# -gt 0 ]]; do
    if [[ "$1" == "--" ]]; then
        shift
        test_args=("$@")
        break
    fi
    cmake_args+=("$1")
    shift
done

# -O0 keeps the line counters faithful (no inlined or merged lines); the CMake file adds the float flags as usual.
# -fkeep-inline-functions emits inline functions nobody calls, so a header's dead helpers count as uncovered lines
# instead of vanishing from the report (uninstantiated templates still cannot be seen). Some of those wrap Windows API
# calls (game/display_geometry.h: GetClientRect) that the tests never reach and the normal build never links, hence
# --warn-unresolved-symbols: a test that did call one would have failed to link in tools/unit_linux.sh already.
cmake -S "$root/tests/unit/linux" -B "$dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_C_FLAGS="-m32" -DCMAKE_CXX_FLAGS="-m32 -O0 --coverage -fkeep-inline-functions" \
    -DCMAKE_EXE_LINKER_FLAGS="-m32 --coverage -Wl,--warn-unresolved-symbols" "${cmake_args[@]}" >/dev/null || exit 1
if ! cmake --build "$dir" >"$dir/build.log" 2>&1; then
    grep -E 'error|undefined reference' "$dir/build.log" | head -20
    echo "build failed: $dir/build.log"
    exit 1
fi

# Counters accumulate across runs: start from zero so the report describes this run only.
find "$dir" -name '*.gcda' -delete
"$dir/bin/obl_unit_tests" "${test_args[@]}" >"$dir/unit.log" 2>&1
rc=$?
grep -E '^[0-9]+ passed, [0-9]+ failed' "$dir/unit.log" | tail -1
grep -E '^FAIL ' "$dir/unit.log" | sed 's/^/  /'

python3 "$root/tools/unit_coverage_report.py" --build "$dir" --repo "$root" \
    --text "$dir/coverage.txt" --json "$dir/coverage.json" || exit 1
echo "report: $dir/coverage.txt"
exit "$rc"
