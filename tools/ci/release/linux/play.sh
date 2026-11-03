#!/usr/bin/env bash
# Starts OpenBeach on your imported copy (README.txt). Settings, saves and the log are in userdata/.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -f "$here/userdata/packs/base/pack.json" ]]; then
    echo "Your copy of Beach Life is not imported yet: run ./import.sh first, see README.txt." >&2
    echo "To try OpenBeach without the original game, run ./demo.sh." >&2
    exit 1
fi
exec "$here/OpenBeach" --game-dir "$here/userdata" "$@"
