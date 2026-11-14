#!/bin/sh
# Starts OpenBeach on the handheld (docs/build/handheld.md). Run it from the game directory (OpenBeach, packs/).
cd "$(dirname "$0")" || exit 1

# The first start plays at the panel's 640x480; the in-game options still offer the other sizes and keep the choice.
# shortcut: only an INI without a resolution is seeded; one that has a [Video] section without ResWidth is left alone.
if ! grep -q '^ResWidth=' BeachLife.ini 2>/dev/null && ! grep -q '^\[Video\]' BeachLife.ini 2>/dev/null; then
    printf '[Video]\r\nResWidth=640\r\nResHeight=480\r\n' >> BeachLife.ini
fi

# SDL3 has no video driver here: its offscreen window only carries the events, and the game's panel thread shows the frames
# through the firmware's SDL2, which the menu's LD_LIBRARY_PATH finds (/usr/lib: Anbernic's build with the Mali driver).
SDL_VIDEO_DRIVER=offscreen SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-alsa} exec ./OpenBeach --game-dir "$PWD" --no-settings "$@"
