#!/usr/bin/env bash
# Starts OpenBeach on your imported copy (README.en.txt). Settings, saves and the log are in userdata/.
set -euo pipefail
# A root process cannot reach the normal user's desktop sound server: no sound, and root-owned files in userdata/.
if [[ "$(id -u)" == 0 ]]; then
    echo "WARNING: you are running as root (sudo). Sound will not work and files in userdata/ become root-owned." >&2
    echo "         Run this as your normal user instead." >&2
fi
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -f "$here/userdata/packs/base/pack.json" ]]; then
    echo "Your copy of Beach Life is not imported yet: run ./import.sh first, see README.en.txt." >&2
    echo "To try OpenBeach without the original game, run ./demo.sh." >&2
    exit 1
fi
exec "$here/OpenBeach" --game-dir "$here/userdata" "$@"
