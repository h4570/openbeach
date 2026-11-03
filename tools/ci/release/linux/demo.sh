#!/usr/bin/env bash
# Starts OpenBeach on the L0 pack, the project's own small scenario: no original game data needed (README.txt).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OBL_PACK=l0 exec "$here/OpenBeach" --game-dir "$here/l0" "$@"
