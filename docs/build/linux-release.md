# Linux release build

Every GitHub release carries `openbeach-<version>-linux-x86_64.tar.gz`, built by the `linux` job of
`.github/workflows/release.yml` ([ci.md](ci.md#release-workflow)), and its debug info as `...-linux-x86_64-debug.tar.gz`.

## The tarball
| File | What it is |
|---|---|
| `OpenBeach` | the windowed game (`-DOBL_LINUX_WINDOWED=ON`), stripped |
| `obl-convert` | the converter from the player's copy, stripped |
| `l0/packs/l0/` | a game directory with the L0 pack: `OBL_PACK=l0 ./OpenBeach --game-dir l0` plays without original data |
| `README.txt` | how to run it and the run-time packages (`tools/ci/linux_release_readme.txt`) |
| `LICENSE`, `THIRD_PARTY_NOTICES.md` | the licences |

The debug archive holds `OpenBeach.debug` and `obl-convert.debug` (`objcopy --only-keep-debug`); the stripped binaries
name them through `.gnu_debuglink`, so `gdb` finds them in the same directory.

## Build
`tools/ci/linux_release.sh` (packages: `tools/ci/linux_release_packages.sh`):
- `tools/obl_sim_linux.sh` with `OBL_SIM_BUILD_TYPE=RelWithDebInfo` (the script's default stays Debug). The game targets
  get `-O2` and the same floating-point flags in every configuration ([../architecture/linux-host.md](../architecture/linux-host.md#toolchain-and-abi)).
- libstdc++ and libgcc link statically (`-static-libstdc++ -static-libgcc`), so only glibc is a shared C/C++ runtime.
- The optimized `obl-sim` of the same build runs the L0 scripts, the L0 replays and `golden_hash_l0_s1` before
  anything is packed: a moved world hash fails the job.
- Each binary's newest glibc symbol must be at most 2.35 (`OBL_GLIBC_MAX`) and no `GLIBCXX_` symbol may remain.

## Portability
The job runs on Ubuntu 22.04: glibc 2.35 is the floor, so Debian 12 (2.36) and Ubuntu 22.04 and later run it. A build
on Ubuntu 24.04 would need glibc 2.38 (`__isoc23_strtol`, `fmod`) and GLIBCXX 3.4.31, which Debian 12 lacks.

SDL3 links statically; the binary itself needs only `libc`, `libm` (and the dynamic loader). SDL loads its drivers at
run time (`dlopen`): X11 (`libX11`, `libXext`, `libXcursor`, `libXi`, `libXfixes`, `libXrandr`, `libXss`), Wayland
(`libwayland-client/cursor/egl`, `libxkbcommon`, `libdecor`), OpenGL/EGL (`libGL`, `libEGL`, GLES) and audio (ALSA,
PulseAudio, PipeWire, JACK, sndio). A missing library only removes that driver: without OpenGL the software renderer
presents, without audio the game is silent. The README names the Debian/Ubuntu packages.

## Smoke test
`tools/ci/linux_smoke.sh <unpacked tarball>` starts the packed `OpenBeach` on the L0 pack under Xvfb (never a real
display) with SDL's X11 driver and the dummy audio driver, waits for the intro movie and the first presented menu frame,
then sends `SIGTERM`; SDL turns it into its quit event, and the test expects `WinMain returned 0`, `process exit` and
exit code 0. `tools/ci/linux_smoke_distros.sh` runs the same test in clean `debian:12` and `ubuntu:24.04` containers that
have only the README's packages and Xvfb. The job keeps a screenshot of the Xvfb screen in its artifact.

Not covered: real input, a real audio device, Wayland and a GPU driver (Xvfb renders OpenGL with Mesa's llvmpipe).

## Testing without a release
Run the workflow by hand (`gh workflow run release.yml --ref <branch>`): the `release` job is skipped, and the `linux`
job builds, tests and packs, then keeps the tarballs and the screenshot as the workflow artifact `linux-release`
(14 days) with the version `<version.txt>-dev.<commit>`. Only a release run attaches them to the release
(`gh release upload <tag> --clobber`).
