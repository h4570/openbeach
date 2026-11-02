#!/usr/bin/env bash
# The CI sim job on a local Linux machine or WSL (docs/build/ci.md, "Local post-merge check"): the L0 scripts without game
# data, then a copy of run/ on the Linux file system, converted by the Linux obl-convert, installed-data unit oracles,
# the headless suite and the first seed of every bot. tools/linux_check.ps1 -Full runs it.
#   tools/linux_sim_check.sh [--all-seeds]
# Needs run/ with the imported game data. Directories: $OBL_SIM_BUILD (obl-sim), $OBL_SIM_GAME (the game copy, kept
# between runs so the sprite conversion is skipped while its stamp matches), $OBL_L0_GAME_DIR (L0 game directory).
# Exit code: 1 when a step fails; every step runs.
set -uo pipefail

root="${OBL_SOURCE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
# Nested LF script streams have no repository path of their own.
export OBL_SOURCE_ROOT="$root"
export OBL_SIM_BUILD="${OBL_SIM_BUILD:-$root/build/linux-sim-x64}"
game="${OBL_SIM_GAME:-$root/build/linux-sim-game}"
export OBL_L0_GAME_DIR="${OBL_L0_GAME_DIR:-$root/build/l0-run}"
seeds=(--first-seed)
[[ "${1:-}" == "--all-seeds" ]] && seeds=()
status=0

step() {
    echo "== $1 =="
    shift
    "$@" || { echo "FAIL step exit $?"; status=1; }
}

if [[ ! -f "$root/run/BeachLife.exe" ]]; then
    echo "linux_sim_check: run/ has no imported game data" >&2
    exit 1
fi

step "L0 tests without game data" bash <(tr -d '\r' < "$root/tools/ci/l0_sim_tests.sh")
# The game copy leaves out what the Windows side writes there (packs from the Windows converter, logs, scratch files):
# the Linux converter must produce packs/ itself, as in CI.
mkdir -p "$game"
step "Game copy" rsync -a --delete --exclude /packs/ --exclude /testlogs/ --exclude /tmp/ "$root/run/" "$game/"
step "Build obl-sim and convert" env OBL_GAME_DIR="$game" bash <(tr -d '\r' < "$root/tools/obl_sim_linux.sh")
step "Installed-data unit oracles on Linux packs" env OBL_UNIT_DATA_ONLY=1 OBL_GAME_DIR="$game" bash <(tr -d '\r' < "$root/tools/unit_linux.sh") clang
step "Headless tests" python3 "$root/tools/obl_sim_tests.py" --no-build --game-dir "$game" --skip 'bot_*'
step "Scenario bots" python3 "$root/tools/bot_runs.py" --game-dir "$game" "${seeds[@]}"
exit "$status"
