#!/usr/bin/env bash
# Smoke test of the macOS OpenBeach.app (docs/build/releases.md): starts the app's launcher with --demo, as a player
# would from the menu, under a temporary HOME, so the demo's game directory lands in that HOME's Application Support.
# Waits until the main loop presents frames in a Cocoa window, asks the game to quit with SIGTERM (SDL turns it into its
# quit event) and expects a clean exit with code 0.
#   tools/ci/macos_smoke.sh <OpenBeach.app> [screenshot.png]
set -euo pipefail

app="$(cd "$1" && pwd)"
shot="${2:-}"
work="$(mktemp -d)"
game=""
cleanup() {
    [[ -n "$game" ]] && kill -KILL "$game" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

mkdir -p "$work/home"
log="$work/home/Library/Application Support/OpenBeach/l0/openbeach.log"
# The launcher replaces itself with the game (exec), so $game is the game's process.
HOME="$work/home" SDL_AUDIO_DRIVER=dummy "$app/Contents/MacOS/openbeach-launcher" --demo --no-settings >"$work/stdout.log" 2>&1 &
game=$!

# The intro movie, then the first frame of the menu: the presenter's texture is made by the first presented frame.
ready=0
for _ in $(seq 600); do
    if ! kill -0 "$game" 2>/dev/null; then break; fi
    if grep -q "RendererConstruct" "$log" 2>/dev/null && grep -q "sdl presenter: texture" "$log" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 0.1
done
if [[ "$ready" != 1 ]]; then
    echo "error: the game did not reach its main loop" >&2
    cat "$work/stdout.log" "$log" >&2 || true
    exit 1
fi
sleep 3 # a few seconds of frames in the menu
if [[ -n "$shot" ]]; then screencapture -x "$shot" || echo "warning: screenshot failed"; fi

kill -TERM "$game"
code=0
for _ in $(seq 150); do kill -0 "$game" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$game" 2>/dev/null; then
    echo "error: the game did not quit within 15 s of SIGTERM" >&2
    cat "$log" >&2
    exit 1
fi
wait "$game" || code=$?
game=""
grep -E "sdl window|sdl presenter|platform-main|process exit" "$log"
if grep -q "OpenBeach icon was not set" "$log"; then
    echo "error: the window has no OpenBeach icon" >&2
    exit 1
fi
if [[ "$code" != 0 ]] || ! grep -q "platform-main: WinMain returned 0" "$log" || ! grep -q "process exit" "$log"; then
    echo "error: unclean exit (code $code)" >&2
    cat "$work/stdout.log" "$log" >&2
    exit 1
fi
echo "smoke test passed: launcher, window, frames and a clean exit"
