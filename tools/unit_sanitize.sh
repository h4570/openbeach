#!/usr/bin/env bash
# obl_unit_tests under AddressSanitizer + UndefinedBehaviorSanitizer on Linux as x86-64 code (docs/testing/unit-tests.md, "ASan/UBSan").
#   tools/unit_sanitize.sh [gcc|clang|all] [-DOPTION=value...] [-- <test name prefixes, -prefix excludes>]
# Same prerequisites as tools/unit_linux.sh, plus the sanitizer runtimes (Ubuntu, Clang: libclang-rt-<N>-dev).
# Build trees: build/linux-asan-<compiler>-m64/ (git-ignored; $OBL_UNIT_BUILD_ROOT replaces build/, as in
# unit_linux.sh); logs: <build tree>/sanitize.log.
#
# The run does not stop at the first report (-fsanitize-recover=all, halt_on_error=0): one pass lists every finding, each
# attributed to the test case that was running (stdout is line-buffered and shares the log with stderr, so a report
# appears before the PASS/FAIL line of its case). Exit code: 1 when any test fails or any sanitizer report exists.
set -uo pipefail

root="${OBL_SOURCE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
which="${1:-all}"
[[ $# -gt 0 ]] && shift
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

# A 64 MB quarantine of freed blocks (ASan's default is 256 MB) still catches a use after free of any recently freed block;
# it was lowered when the 32-bit build ran out of address space late in a whole run.
export ASAN_OPTIONS="${ASAN_OPTIONS:-halt_on_error=0:detect_leaks=1:detect_stack_use_after_return=1:strict_string_checks=1:check_initialization_order=1:abort_on_error=0:quarantine_size_mb=64}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=0:print_stacktrace=1}"

status=0

run_one() {
    local cc="$1" cxx="$2"
    local dir="${OBL_UNIT_BUILD_ROOT:-$root/build}/linux-asan-$1-m64"
    shift 2
    local san="-fsanitize=address,undefined -fsanitize-recover=all -fno-omit-frame-pointer -g"
    echo "== $cc (ASan+UBSan) =="
    # Both compilers keep the optimizer on (-O1) so the sanitizers also see optimized code.
    cmake -S "$root/tests/unit/linux" -B "$dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
        -DCMAKE_C_FLAGS="-O1 $san" -DCMAKE_CXX_FLAGS="-O1 $san" \
        -DCMAKE_EXE_LINKER_FLAGS="$san" "$@" >/dev/null || { status=1; return; }
    cmake --build "$dir" || { status=1; return; }

    local log="$dir/sanitize.log"
    "$dir/bin/obl_unit_tests" "${test_args[@]}" >"$log" 2>&1
    local rc=$?

    # A report starts with "file:line:col: runtime error:" (UBSan) or "ERROR: AddressSanitizer|LeakSanitizer" (ASan/LSan).
    # The case it belongs to is the first PASS/FAIL/SKIP line after it.
    awk '
        /runtime error:|ERROR: (Address|Leak)Sanitizer|SUMMARY: (Address|Leak)Sanitizer/ {
            if ($0 ~ /SUMMARY:/) next
            pending[++n] = $0
            next
        }
        /^(PASS|FAIL|SKIP) / {
            for (i = 1; i <= n; ++i) printf "  [%s] %s\n", $2, pending[i]
            n = 0
        }
        END { for (i = 1; i <= n; ++i) printf "  [after the last case] %s\n", pending[i] }
    ' "$log" | sort | uniq -c | sed 's/^ *//' >"$dir/findings.txt"
    local findings
    findings=$(wc -l <"$dir/findings.txt")
    local summary
    summary=$(grep -E '^[0-9]+ passed, [0-9]+ failed' "$log" | tail -1)
    echo "${summary:-no summary line (crash?)}; exit code $rc; distinct sanitizer findings: $findings"
    if [[ "$findings" -gt 0 ]]; then
        echo "-- findings (count, test case, first line of the report; full text in $log) --"
        cat "$dir/findings.txt"
    fi
    grep -E '^FAIL ' "$log" | sed 's/^/  /'
    if [[ "$rc" -ne 0 || "$findings" -gt 0 ]]; then status=1; fi
}

case "$which" in
    gcc) run_one gcc g++ "${cmake_args[@]}" ;;
    clang) run_one clang clang++ "${cmake_args[@]}" ;;
    all) run_one gcc g++ "${cmake_args[@]}"; run_one clang clang++ "${cmake_args[@]}" ;;
    *) echo "usage: $0 [gcc|clang|all] [-DOPTION=value...] [-- test prefixes]" >&2; exit 2 ;;
esac
exit "$status"
