# Handheld build (aarch64, Anbernic stock firmware)

A build of the windowed OpenBeach for handhelds that run Linux with a pad but no window system: first target the
Anbernic RG40XXV (Allwinner H700, 4x Cortex-A53, about 1 GB RAM) with its stock firmware (Ubuntu 22.04, glibc 2.35, no `/dev/dri`,
no Wayland, `/dev/fb0` only). In play the game holds about 280-300 MB. Experimental: not part of CI or the releases.

## Build
```
pip install ziglang cmake ninja       # or any zig, cmake and ninja on PATH
python tools/obl_handheld.py          # build/handheld-aarch64/bin/OpenBeach
```
`tools/obl_handheld.py` is `tools/obl_sim_linux.sh` for this target: Zig is the C/C++ compiler (`-target aarch64-linux-gnu.2.35`, no
container) and `host_linux/CMakeLists.txt` gets `-DOBL_HANDHELD=ON`. `--glibc` sets the oldest glibc the program runs on, `--zig
"<command>"` the compiler command. The pack is converted on the PC and copied to the device.

From the device the build needs only headers and one library, in `build/handheld-sysroot/` (`--sysroot` names another folder):
```
ssh root@<device> 'tar cf - -C /usr/include SDL2 alsa' | tar xf - -C build/handheld-sysroot/include
ssh root@<device> 'cat /usr/lib/aarch64-linux-gnu/libasound.so.2.0.0' > build/handheld-sysroot/lib/libasound.so
```
The SDL2 headers are the firmware's (2.0.12, the version of Anbernic's library with the Mali driver); SDL3's ALSA audio and the
panel's volume use ALSA.

## What `OBL_HANDHELD` changes
| Part | Behaviour |
|---|---|
| SDL | The game's SDL3 is built with its offscreen video driver and software renderer only (no X11, Wayland, KMS, GL); it carries the window and the events. Audio is its ALSA driver (the device's `default` PCM, the speaker). |
| Picture | The panel shows only what the firmware's own SDL2 puts on it: Anbernic's build in `/usr/lib` has the Mali video driver, the game's static SDL3 has none, and a write to `/dev/fb0` never reaches the panel. So the game shows its frames through that SDL2, in a thread (`platform/frame_share.cpp`). SDL2 and SDL3 share their function names, so the library is loaded with `dlopen` (`RTLD_DEEPBIND`: SDL2 looks up its own symbols before the game's, which `-rdynamic` exports) and called only through pointers from `dlsym`; only that file sees SDL2's headers. The game copies each 16 bpp frame for the thread under a lock; the thread converts it to RGBA (SDL2's GLES renderer has no 16 bpp texture format) in bands of 16 rows, and only the bands that changed since the last frame are converted and uploaded (`platform/frame_bands.h`), because the Mali's texture upload is the costly part. It shows the frame letterboxed with linear filtering at any game resolution (the menus and screens before the game are always 800x600, shown at 0.8 on the 640x480 panel; the `[Video]` size applies to the game itself) and draws the cursor. The first start writes `[Video] ResWidth=640 ResHeight=480` into `BeachLife.ini` (`launch.sh`, the panel's size); the in-game options offer the other sizes and keep the choice. |
| Pointer | `input.cpp` + `pad_pointer.h`: the pad is read from the kernel joystick device `/dev/input/js0` (`OBL_PAD_DEVICE`), because SDL3 does not classify the firmware's input devices as joysticks. The left stick moves the cursor (dead zone, squared speed curve, at most 1100 px/s at full push and 100 %); the speed is `[Video] CursorSpeed` in `BeachLife.ini` (default 30, never below 10), set by a slider this build adds to the game's Video options ("Ustawienia grafiki"; `ui/own_controls.cpp`). A second slider, `[Video] ScrollSpeed` (default 100 = the original, never below 10), scales the map's scrolling by the screen edge and by the arrow keys, which a stick-moved cursor otherwise reaches too easily (`UpdateCameraInput`). Both exist only where a stick is the pointer (`own_controls::PadPointer`): other builds neither show the sliders nor save the keys, and scroll at the original speed; L1 or A is the left button, R1 or B the right one. The right button starts the camera drag, and the stick then drags the map. The D-pad is the arrow keys, which scroll the map. SELECT is Esc (the game's menu) and X is Space (turns the building being placed). No SDL relative mode is used. |
| Pad numbers | The kernel numbers axes and buttons by their evdev codes, and this firmware's `gamecontrollerdb` gives the RG40XXV's: A 0, B 1, L1 4, R1 5, SELECT 6, START 7, the stick on axes 0 and 1, the D-pad on the hat axes 4 and 5 (`kPadButtonA` ... in `input.cpp`). `OBL_PAD_L1`, `OBL_PAD_R1`, `OBL_PAD_AXIS_X` and `OBL_PAD_AXIS_Y` change the first four; the log says when the pad opened. |
| Volume | The firmware's volume keys have no handler during a port, so the panel thread reads them (`/dev/input/event*` named `ANBERNIC-keys`), moves ALSA's `lineout volume` on card `hw:0` in 16 steps and draws a bar with the level for 1.5 s. The first start sets half of the console's maximum (the console itself starts at full volume, too loud for the game); the chosen level is kept in `obl_volume.txt` in the game directory, and the console's own level is put back when the game ends. |
| Intro movie | Goes to the panel like the game's frames (the software renderer's image, read back). |
| Sprites | Decoding a guest's sheet takes the A53 150-375 ms, and the game decodes it in the frame that first draws the animation. This build decodes animations in a background thread ahead of time (`ui::EnableSpritePrefetch`, [sprites.md](../architecture/sprites.md)): those of objects within half a view of the screen, and the other directions of one that just loaded. A guest that starts a new activity on screen still waits for one decode. |

## Run
`tools/handheld/launch.sh` (copied beside `OpenBeach`) sets the environment and starts the game on its own directory (`--game-dir`, no
settings window). The directory holds `packs/base/`, `remaster/` and `BeachLife.ini`, which `Import.cmd` writes to `userdata/` of a
release: copy that folder's contents beside the binary. A script in `Roms/PORTS` starts it from the console's menu, for example
`OpenBeach.sh` holding `#!/bin/sh` and `exec /mnt/sdcard/ports/OpenBeach/launch.sh`. The game runs with `SDL_VIDEO_DRIVER=offscreen` and its window at the game's size (`Windowed=1`), so that client pixels are
game pixels.

