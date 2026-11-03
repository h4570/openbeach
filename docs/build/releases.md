# Release archives

Every GitHub release carries a player's archive per system, built by `.github/workflows/release.yml`
([ci.md](ci.md#release-workflow)), and the debug info beside it:

| Asset | Job | Debug info |
|---|---|---|
| `openbeach-<version>-windows-x64.zip` | `windows` | `...-windows-x64-debug.zip` (`OpenBeach.pdb`, `obl-convert.pdb`) |
| `openbeach-<version>-linux-x86_64.tar.gz` | `linux` | `...-linux-x86_64-debug.tar.gz` (`objcopy --only-keep-debug`; the stripped binaries name it through `.gnu_debuglink`) |

## Layout
Three entries at the top, so a player sees where to start:

| Path | What it is |
|---|---|
| `README.txt` | the steps for people in the six languages of the game (`tools/ci/release/README.txt.in`) |
| `AGENTS.md` | the same steps for an AI agent, in English (`tools/ci/release/AGENTS.md.in`) |
| `game/OpenBeach[.exe]`, `game/obl-convert[.exe]` | the game and the converter (Linux: stripped) |
| `game/Import.cmd`, `Play.cmd`, `Demo.cmd`, `scripts/import.ps1` (Windows); `game/import.sh`, `play.sh`, `demo.sh` (Linux) | the player's commands (`tools/ci/release/<windows\|linux>/`) |
| `game/userdata/` | the game directory of the player's copy, empty until the import |
| `game/l0/packs/l0/` | the demo's game directory with the L0 pack (`OBL_PACK=l0`) |
| `game/LICENSE`, `game/THIRD_PARTY_NOTICES.md` | the licences |

`tools/ci/release/stage.py` lays this out for both systems: a template line starting with `[win]` or `[linux]` is kept
only on that system, and the Windows README gets backslash paths and CRLF line endings. Batch files are checked out with
CRLF (`.gitattributes`): `cmd.exe` misreads labels in LF files.

**The import script** runs `obl-convert import <copy> userdata/packs/base` (replacing an older pack whole) and
`obl-convert resources <copy> userdata/remaster`, then sets `[Language] Language=<index>` in `userdata/BeachLife.ini` to
the pack's language (`pack.json`), adding `[Beach Life Options] Version=1` to a new file: the game reads nothing from an
INI without it and would start in English. A CD names no language, so on the converter's language error the script asks
for one and imports again. An `.iso` is mounted for the import (`Mount-DiskImage`, `udisksctl loop-setup`) and unmounted
afterwards. On Windows a dropped drive root `E:\` loses its trailing backslash in `Import.cmd`, because `\"` would escape
the quote, and `import.ps1` puts it back.

**The default game directory.** Without `--game-dir` the game takes its own directory when it holds `Data/` (an installed
game), else `userdata/` beside the program (a release), else `OBL_GAME_DIR` (`run/` of the build tree)
(`DefaultGameDir`, `src/launcher/remaster_main.cpp`). The release name is not `data`: on Windows that is the same
directory as `Data`.

## Build
**Windows** (`tools/ci/windows_release.ps1`): the `x64` preset with `-DOBL_CONVERT_ON_BUILD=OFF`, RelWithDebInfo, the
static MSVC runtime of every build, so the zip needs no redistributable. `tools/runtests.ps1 -Pack public -Config
RelWithDebInfo` then runs the same L0 set as Linux before anything is packed. The job runs on `windows-2025`, which has
Visual Studio 2026 (the preset's generator).

**Linux** (`tools/ci/linux_release.sh`, packages: `tools/ci/linux_release_packages.sh`):
- `tools/obl_sim_linux.sh` with `OBL_SIM_BUILD_TYPE=RelWithDebInfo` (the script's default stays Debug). The game targets
  get `-O2` and the same floating-point flags in every configuration ([../architecture/linux-host.md](../architecture/linux-host.md#toolchain-and-abi)).
- libstdc++ and libgcc link statically (`-static-libstdc++ -static-libgcc`), so only glibc is a shared C/C++ runtime.
- The optimized `obl-sim` of the same build runs the L0 scripts, the L0 replays and `golden_hash_l0_s1` before
  anything is packed: a moved world hash fails the job.
- Each binary's newest glibc symbol must be at most 2.35 (`OBL_GLIBC_MAX`) and no `GLIBCXX_` symbol may remain.

## Linux portability
The job runs on Ubuntu 22.04: glibc 2.35 is the floor, so Debian 12 (2.36) and Ubuntu 22.04 and later run it. A build
on Ubuntu 24.04 would need glibc 2.38 (`__isoc23_strtol`, `fmod`) and GLIBCXX 3.4.31, which Debian 12 lacks.

SDL3 links statically; the binary itself needs only `libc`, `libm` (and the dynamic loader). SDL loads its drivers at
run time (`dlopen`): X11 (`libX11`, `libXext`, `libXcursor`, `libXi`, `libXfixes`, `libXrandr`, `libXss`), Wayland
(`libwayland-client/cursor/egl`, `libxkbcommon`, `libdecor`), OpenGL/EGL (`libGL`, `libEGL`, GLES) and audio (ALSA,
PulseAudio, PipeWire, JACK, sndio). A missing library only removes that driver: without OpenGL the software renderer
presents, without audio the game is silent. The README names the Debian/Ubuntu packages in every language section.

## Smoke tests
- **Windows:** `tools/ci/windows_smoke.ps1 <unpacked zip>` starts `game\OpenBeach.exe --hidden` on a copy of `game\l0`
  with `OBL_PACK=l0` and the dummy audio driver, waits for the first presented menu frame and a few seconds in the menu,
  then stops the host.
- **Linux:** `tools/ci/linux_smoke.sh <unpacked tarball>` starts `game/OpenBeach` the same way under Xvfb (never a real
  display) with SDL's X11 driver, then sends `SIGTERM`; SDL turns it into its quit event, and the test expects `WinMain
  returned 0`, `process exit` and exit code 0. `tools/ci/linux_smoke_distros.sh` runs it in clean `debian:12` and
  `ubuntu:24.04` containers with only the README's packages (its English section, the first list) and Xvfb. The job
  keeps a screenshot of the Xvfb screen in its artifact.

Not covered: the import scripts (CI has no copy of the game), real input, a real audio device, Wayland and a GPU driver.

## Testing without a release
Run the workflow by hand (`gh workflow run release.yml --ref <branch>`): the `release` job is skipped, and `linux` and
`windows` build, test and pack, then keep the archives as the workflow artifacts `linux-release` and `windows-release`
(14 days) with the version `<version.txt>-dev.<commit>`. Only a release run attaches them to the release
(`gh release upload <tag> --clobber`). Locally: `./tools/ci/windows_release.ps1 [-SkipTests]`, and in WSL
`bash tools/ci/linux_release.sh` (an LF copy of the checkout, [../testing/unit-tests.md](../testing/unit-tests.md)).
