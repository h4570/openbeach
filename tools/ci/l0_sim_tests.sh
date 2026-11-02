#!/usr/bin/env bash
# The L0 scenario tests on obl-sim without any game data of the player (docs/build/ci.md): builds obl-sim, makes a game
# directory that holds only the committed own pack (data/packs/l0/) and runs the @headless L0 scripts on it with
# OBL_PACK=l0 (their @env header). A loader reaching for a converted or imported file stops the run (FatalError).
#   tools/ci/l0_sim_tests.sh [extra cmake -D options...]
# Game directory: build/l0-run/ (git-ignored), or $OBL_L0_GAME_DIR; it is recreated on every run.
set -euo pipefail

root="${OBL_SOURCE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
# Nested LF script streams have no repository path of their own.
export OBL_SOURCE_ROOT="$root"
game="${OBL_L0_GAME_DIR:-$root/build/l0-run}"
build="${OBL_SIM_BUILD:-$root/build/linux-sim-x64}"

OBL_SIM_SKIP_CONVERSION=1 OBL_SIM_BUILD="$build" bash <(tr -d '\r' < "$root/tools/obl_sim_linux.sh") "$@"
rm -rf "$game"
# Data/ stays empty: it only marks the directory as a game directory for obl_sim_tests.py.
mkdir -p "$game/packs/l0" "$game/Data"
cp -R "$root/data/packs/l0/." "$game/packs/l0/"
# A settings file of the current version, as a played install has: without it the game ignores the @gameini values
# (settings::LoadMain keeps the defaults) and l0_gameplay reads language 0.
printf '[Beach Life Options]\nVersion=1\n' > "$game/BeachLife.ini"
python3 "$root/tools/obl_sim_tests.py" --no-build --build-dir "$build" --game-dir "$game" \
    --filter l0_boot l0_gameplay l0_guests l0_goal l0_builders "l0_bot_*"
