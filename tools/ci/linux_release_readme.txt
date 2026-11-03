OpenBeach @VERSION@ for Linux x86-64
=====================================

OpenBeach is an open-source engine for the 2002 resort tycoon Beach Life (GPL-3.0-or-later, see LICENSE;
third-party components in THIRD_PARTY_NOTICES.md). It contains no data of the original game.
Source and documentation: https://github.com/h4570/openbeach

Contents
  OpenBeach      the game
  obl-convert    the converter that reads your own copy of Beach Life once
  l0/            a game directory with the L0 pack, the project's own small scenario (no original data needed)

Requirements
  x86-64, glibc 2.35 or newer (Debian 12, Ubuntu 22.04 and later), an X11 or Wayland desktop.
  SDL is built in and loads the system libraries at run time. A desktop install has them; otherwise:
    sudo apt install libx11-6 libxext6 libxcursor1 libxi6 libxfixes3 libxrandr2 libxss1 \
                     libwayland-client0 libwayland-cursor0 libwayland-egl1 libxkbcommon0 libdecor-0-0 \
                     libgl1 libegl1 libasound2 libpulse0
  (on Ubuntu 24.04 and later the ALSA package is libasound2t64).
  Without OpenGL the game presents with SDL's software renderer; without a sound server it plays silently.

Try it without game data (the L0 pack)
  OBL_PACK=l0 ./OpenBeach --game-dir l0

Play the original scenarios from your own copy (an installation or the CD)
  mkdir -p ~/beachlife/packs/base ~/beachlife/remaster
  ./obl-convert import "<your Beach Life folder>" ~/beachlife/packs/base     # about 15 minutes, once
  ./obl-convert resources "<your Beach Life folder>" ~/beachlife/remaster    # window icon and cursors
  ./OpenBeach --game-dir ~/beachlife

The game writes its settings (openbeach.ini), saves and log (openbeach.log) into the game directory.
Linux support is experimental: report problems with the log at https://github.com/h4570/openbeach/issues
