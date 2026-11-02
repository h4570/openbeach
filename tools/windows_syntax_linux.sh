#!/usr/bin/env bash
# Syntax-checks the DLL shell, launcher and Windows-only platform sources on Linux.
# These files have no Windows compile coverage in the Linux unit/simulation jobs. Clang with the x86_64 MinGW target and the
# MinGW-w64 headers/libstdc++ parses MSVC inline asm (__asm { }), __try/__except and
# __declspec; it only parses (-fsyntax-only): no objects, no link, no game run. Remaining MSVC
# gaps are covered by tools/windows_syntax_linux/msvc_compat.h, injected with -include here only.
#
# Usage: tools/windows_syntax_linux.sh [-j N] [-v] [-k] [pattern...]
#   pattern  substring filter on the repo-relative path (e.g. "render/" or "sim_guest")
#   -j N     parallel jobs (default: nproc)
#   -v       print the full compiler output for each failing file (default: first 20 lines)
#   -k       print only the summary line (the exit code still reports failures)
#   --compdb only write build/linux-syntax/compile_commands.json for the selected files (tools/tidy.py) and exit
# Exit code: 0 when every checked file passes, 1 otherwise.
# Dependencies (pinned like the root CMakeLists.txt) are fetched once into build/linux-syntax/deps.
set -euo pipefail

ROOT="${OBL_SOURCE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
HERE="$ROOT/tools/windows_syntax_linux"
DEPS="$ROOT/build/linux-syntax/deps"
LOGDIR="$ROOT/build/linux-syntax/logs"
CXX="${OBL_SYNTAX_CXX:-clang++}"
CC="${OBL_SYNTAX_CC:-clang}"
TARGET=x86_64-w64-windows-gnu

JOBS="$(nproc 2>/dev/null || echo 4)"
VERBOSE=0
QUIET=0
COMPDB=0
PATTERNS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        -j) JOBS="$2"; shift 2 ;;
        -v) VERBOSE=1; shift ;;
        -k) QUIET=1; shift ;;
        --compdb) COMPDB=1; shift ;;
        -h|--help) sed -n '2,15p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) PATTERNS+=("$1"); shift ;;
    esac
done

# clang parses the MSVC-only syntax; the MinGW-w64 package supplies the Windows SDK headers and the
# libstdc++ (posix threads model, needed for std::mutex/std::thread) that clang picks up for $TARGET.
if ! command -v "$CXX" >/dev/null 2>&1 || ! command -v "$CC" >/dev/null 2>&1; then
    echo "error: $CXX/$CC not found (apt-get install -y clang)" >&2
    exit 2
fi
if [[ ! -d /usr/x86_64-w64-mingw32/include ]] || ! ls -d /usr/lib/gcc/x86_64-w64-mingw32/*-posix >/dev/null 2>&1; then
    echo "error: MinGW-w64 x86_64 headers not found. Install them with:" >&2
    echo "  apt-get install -y g++-mingw-w64-x86-64-posix" >&2
    exit 2
fi

fetch_file() { # url sha256 dest
    local url="$1" sha="$2" dest="$3"
    if [[ -f "$dest" ]] && echo "$sha  $dest" | sha256sum -c --status; then return; fi
    mkdir -p "$(dirname "$dest")"
    curl -fsSL --retry 3 -o "$dest.tmp" "$url"
    echo "$sha  $dest.tmp" | sha256sum -c --status || { echo "error: sha256 mismatch for $url" >&2; exit 2; }
    mv "$dest.tmp" "$dest"
}

fetch_git() { # url tag dest [sparse paths...]
    local url="$1" tag="$2" dest="$3"; shift 3
    if [[ -f "$dest/.obl_tag" ]] && [[ "$(cat "$dest/.obl_tag")" == "$tag" ]]; then return; fi
    rm -rf "$dest"
    if [[ $# -gt 0 ]]; then
        git -c advice.detachedHead=false clone -q --depth 1 --branch "$tag" --filter=blob:none --sparse "$url" "$dest"
        git -C "$dest" sparse-checkout set "$@"
    else
        git -c advice.detachedHead=false clone -q --depth 1 --branch "$tag" "$url" "$dest"
    fi
    echo "$tag" > "$dest/.obl_tag"
}

# Keep these pins in sync with the root CMakeLists.txt.
fetch_git https://github.com/libsdl-org/SDL.git release-3.4.16 "$DEPS/SDL3" include
fetch_file https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz \
    d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d "$DEPS/json.tar.xz"
if [[ ! -f "$DEPS/json/include/nlohmann/json.hpp" ]]; then
    tar -xJf "$DEPS/json.tar.xz" -C "$DEPS"
fi
fetch_file https://raw.githubusercontent.com/phoboslab/pl_mpeg/c871f2be022ece7ef4f64230b4fb8e1fb9eb6023/pl_mpeg.h \
    3a8cb30c83c2a1147719c30fe0c8b93da2987aa43140077c575b39aaa75fc2c9 "$DEPS/pl_mpeg/pl_mpeg.h"
fetch_file https://raw.githubusercontent.com/mackron/dr_libs/dfe8377631000664666519fdb83da193fd8037f4/dr_mp3.h \
    997b7ee18de6e6b81e2a83f1ea9fc62aef25c62b28d48db95635f49e65de0a2f "$DEPS/dr_mp3/dr_mp3.h"

# Same definitions and include directories as src/dll/CMakeLists.txt (+ root add_compile_definitions).
COMMON_FLAGS=(
    --target=$TARGET -fsyntax-only -fms-extensions -fasm-blocks -Wno-unknown-pragmas
    '-DOBL_DEFAULT_GAME_DIR="."'
    -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS
    -I"$ROOT/src/common" -I"$ROOT/src"
    -I"$DEPS/pl_mpeg" -I"$DEPS/dr_mp3"
    -I"$DEPS/json/include" -I"$DEPS/SDL3/include"
    -include "$HERE/msvc_compat.h"
)
CXX_FLAGS=(-std=c++20 "${COMMON_FLAGS[@]}")
C_FLAGS=(-std=c11 "${COMMON_FLAGS[@]}")

# Files that cannot be checked here, one repo-relative path per line, "# reason" after it.
declare -A EXCLUDED=()
while IFS= read -r line; do
    path="${line%%#*}"; path="${path//[[:space:]]/}"
    [[ -n "$path" ]] && EXCLUDED["$path"]=1
done < "$HERE/exceptions.txt"

WINDOWS_SOURCES="$(python3 "$ROOT/tools/windows_syntax_sources.py")"
mapfile -t FILES < <(cd "$ROOT" && { find src/dll src/launcher -type f \( -name '*.cpp' -o -name '*.c' \);
    printf '%s\n' "$WINDOWS_SOURCES"; } | sort -u)
SELECTED=()
for f in "${FILES[@]}"; do
    [[ -n "${EXCLUDED[$f]:-}" ]] && continue
    if [[ ${#PATTERNS[@]} -gt 0 ]]; then
        hit=0
        for p in "${PATTERNS[@]}"; do [[ "$f" == *"$p"* ]] && hit=1; done
        [[ $hit -eq 0 ]] && continue
    fi
    SELECTED+=("$f")
done

if [[ ${#SELECTED[@]} -eq 0 ]]; then
    echo "error: no Windows sources selected" >&2
    exit 2
fi

if [[ $COMPDB -eq 1 ]]; then
    # The same flags as the check, minus -fsyntax-only (clang-tidy parses anyway).
    python3 - "$ROOT" "$CXX" "$CC" "${#CXX_FLAGS[@]}" "${CXX_FLAGS[@]}" "${#C_FLAGS[@]}" "${C_FLAGS[@]}" "${SELECTED[@]}" <<'EOF'
import json, sys
root, cxx, cc, rest = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
n = int(rest[0]); cxx_flags = [f for f in rest[1:1 + n] if f != "-fsyntax-only"]; rest = rest[1 + n:]
n = int(rest[0]); c_flags = [f for f in rest[1:1 + n] if f != "-fsyntax-only"]; files = rest[1 + n:]
db = [{"directory": root, "file": f"{root}/{f}",
       "arguments": [cc if f.endswith(".c") else cxx, *(c_flags if f.endswith(".c") else cxx_flags), "-c", f"{root}/{f}"]}
      for f in files]
with open(f"{root}/build/linux-syntax/compile_commands.json", "w") as out:
    json.dump(db, out, indent=1)
print(f"windows_syntax_linux: {len(db)} entries -> build/linux-syntax/compile_commands.json")
EOF
    exit 0
fi

rm -rf "$LOGDIR"; mkdir -p "$LOGDIR"
export ROOT LOGDIR CXX CC
export CXX_FLAGS_STR="$(printf '%q ' "${CXX_FLAGS[@]}")"
export C_FLAGS_STR="$(printf '%q ' "${C_FLAGS[@]}")"
check_one() {
    local f="$1" log="$LOGDIR/${1//\//_}.log"
    if [[ "$f" == *.c ]]; then
        eval "\"$CC\" $C_FLAGS_STR \"$ROOT/$f\"" >"$log" 2>&1
    else
        eval "\"$CXX\" $CXX_FLAGS_STR \"$ROOT/$f\"" >"$log" 2>&1
    fi
    if [[ $? -eq 0 ]]; then rm -f "$log"; echo "PASS $f"; else echo "FAIL $f"; fi
}
export -f check_one

RESULTS="$(printf '%s\n' "${SELECTED[@]}" | xargs -P "$JOBS" -I{} bash -c 'check_one "$@"' _ {} | sort -k2)"
FAILED="$(grep '^FAIL ' <<<"$RESULTS" | cut -d' ' -f2- || true)"
nfail=0
if [[ -n "$FAILED" ]]; then
    nfail="$(wc -l <<<"$FAILED")"
    if [[ $QUIET -eq 0 ]]; then
        while IFS= read -r f; do
            log="$LOGDIR/${f//\//_}.log"
            echo "=== FAIL $f"
            if [[ $VERBOSE -eq 1 ]]; then cat "$log"; else grep -m 20 -E 'error|fatal' "$log" || head -n 20 "$log"; fi
        done <<<"$FAILED"
    fi
fi
echo "windows_syntax_linux: ${#SELECTED[@]} checked, $((${#SELECTED[@]} - nfail)) passed, $nfail failed, ${#EXCLUDED[@]} excluded (logs: build/linux-syntax/logs)"
[[ $nfail -eq 0 ]]
