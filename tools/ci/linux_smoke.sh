#!/usr/bin/env bash
# Smoke test of the windowed Linux OpenBeach on a virtual X server (docs/build/releases.md): never a real display.
# Starts the game on the L0 pack under Xvfb with SDL's X11 driver, waits until the main loop presents frames, asks it
# to quit with SIGTERM (SDL turns it into its quit event) and expects a clean exit with code 0.
#   tools/ci/linux_smoke.sh <unpacked release directory> [screenshot.png]
# Needs Xvfb; the screenshot also needs xwd and ImageMagick's convert, and is skipped without them.
set -euo pipefail

dir="$(cd "$1" && pwd)"
shot="${2:-}"
work="$(mktemp -d)"
xvfb=""
game=""
cleanup() {
    [[ -n "$game" ]] && kill -KILL "$game" 2>/dev/null || true
    [[ -n "$xvfb" ]] && kill "$xvfb" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

# The game writes its ini, log and saves into the game directory: a copy keeps the unpacked release untouched.
cp -R "$dir/game/l0" "$work/game"
log="$work/game/openbeach.log"

Xvfb -displayfd 3 -screen 0 1280x1024x24 -nolisten tcp 3>"$work/display" 2>"$work/xvfb.log" &
xvfb=$!
for _ in $(seq 100); do [[ -s "$work/display" ]] && break; sleep 0.1; done
[[ -s "$work/display" ]] || { echo "error: Xvfb did not start" >&2; cat "$work/xvfb.log" >&2; exit 1; }
display=":$(cat "$work/display")"

env -u WAYLAND_DISPLAY DISPLAY="$display" SDL_VIDEO_DRIVER=x11 SDL_AUDIO_DRIVER=dummy OBL_PACK=l0 \
    XDG_RUNTIME_DIR="$work" "$dir/game/OpenBeach" --game-dir "$work/game" >"$work/stdout.log" 2>&1 &
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
if [[ -n "$shot" ]] && command -v xwd >/dev/null && command -v convert >/dev/null; then
    DISPLAY="$display" xwd -root -silent | convert xwd:- "$shot" || echo "warning: screenshot failed"
fi

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
echo "smoke test passed: window, frames and a clean exit on $display"
