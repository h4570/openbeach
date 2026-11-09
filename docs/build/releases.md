# Release archives

Every GitHub release carries a player's archive per system, a Windows installer and an experimental macOS disk image,
built by `.github/workflows/release.yml` ([ci.md](ci.md#release-workflow)):

| Asset | Job |
|---|---|
| `openbeach-<version>-windows-x64.zip` | `windows` |
| `openbeach-<version>-windows-x64-installer.exe` | `windows`, from the same staged directory as the zip ([Installer](#installer)) |
| `openbeach-<version>-linux-x86_64.tar.gz` | `linux` |
| `openbeach-<version>-macos-arm64.dmg` | `macos`, experimental ([macOS](#macos)) |

No debug info is published: the PDBs stay in the build tree, and the Linux and macOS binaries are stripped (the exported
symbols stay, so a crash report still names exported functions).

## Layout
The archive puts the guides at the top, so a player sees where to start:

| Path | What it is |
|---|---|
| `README.<language>.txt` | the steps for people, one file per game language (`tools/ci/release/README.<language>.txt.in`) |
| `AGENTS.md` | the same steps for an AI agent, in English (`tools/ci/release/AGENTS.md.in`) |
| `game/OpenBeach[.exe]`, `game/obl-convert[.exe]` | the game and the converter (Linux: stripped) |
| `game/Import.cmd`, `Play.cmd`, `Demo.cmd`, `scripts/import.ps1` (Windows); `game/import.sh`, `play.sh`, `demo.sh` (Linux) | the player's commands (`tools/ci/release/<windows\|linux>/`) |
| `game/userdata/` | the game directory of the player's copy, empty until the import |
| `game/l0/packs/l0/` | the demo's game directory with the L0 pack (`OBL_PACK=l0`) |
| `game/LICENSE`, `game/THIRD_PARTY_NOTICES.md` | the licences |
| `game/openbeach.png` (Linux) | the icon of the applications menu entry `import.sh` adds |

`tools/ci/release/stage.py` lays this out for every system: a template line starting with `[win]`, `[linux]` or `[mac]`,
or several of them as `[win|linux]`, is kept only on those systems, and the Windows README gets backslash paths and CRLF
line endings. The macOS layout is different ([macOS](#macos)). Batch files are checked out with
CRLF (`.gitattributes`): `cmd.exe` misreads labels in LF files.

**The import script** runs `obl-convert import <copy> userdata/packs/base` (replacing an older pack whole) and
`obl-convert resources <copy> userdata/remaster` (the cursors), then sets `[Language] Language=<index>` in `userdata/BeachLife.ini` to
the pack's language (`pack.json`), adding `[Beach Life Options] Version=1` to a new file: the game reads nothing from an
INI without it and would start in English. A CD names no language, so on the converter's language error the script asks
for one and imports again. An `.iso` is mounted for the import (`Mount-DiskImage`, `udisksctl loop-setup`) and unmounted
afterwards. On Windows a dropped drive root `E:\` loses its trailing backslash in `Import.cmd`, because `\"` would escape
the quote, and `import.ps1` puts it back.

**The default game directory.** Without `--game-dir` the game takes its own directory when it holds `Data/` (an installed
game), else `userdata/` beside the program (a release), else `OBL_GAME_DIR` (`run/` of the build tree)
(`DefaultGameDir`, `src/launcher/remaster_main.cpp`). The release name is not `data`: on Windows that is the same
directory as `Data`.

## Installer
`tools/ci/release/installer/openbeach.iss` (Inno Setup 6) packs the staged release, in the six game languages:
- **Per user** (`PrivilegesRequired=lowest`, default `%LOCALAPPDATA%/Programs/OpenBeach`): the game writes into
  `game/userdata/`, which Program Files would forbid. The layout is the zip's, READMEs at the top.
- **Pages:** the directory, the `.iso` of the player's CD, the game language (preselected from the installer's language,
  on an update from `BeachLife.ini`), the desktop shortcut (a task, on by default). An update over an imported copy may
  leave the `.iso` empty and imports nothing.
- **Import:** after the files, `game/scripts/import.ps1 -Source <iso> -PreferLanguage <code> -PauseOnError` in a console
  window. `-PreferLanguage` passes `obl-convert import --prefer-language`: the chosen language when the CD has it, else
  one it has, so the import never asks. A failure keeps the window open, then the installer names the Start menu entry
  "Import Beach Life" (`Import.cmd`).
- **Finish:** opens `README.<code>.txt` of the language in `BeachLife.ini` after the import (the converter's fallback can
  differ from the choice).
- **Shortcuts** start `OpenBeach.exe` minimized (it is a console program, like `Play.cmd`'s `start /min`). The uninstaller
  removes the converted copy (`userdata/packs`, `userdata/remaster`) and keeps saves and settings.
- **Command line:** `/ISO=<file>`, `/GAMELANG=<code>`. A silent run without `/ISO` installs only: the CI smoke test.

`tools/ci/windows_release.ps1` finds `ISCC.exe` (`ISCC`, `PATH`, Program Files or `%LOCALAPPDATA%/Programs/Inno Setup 6`,
the per-user `winget install JRSoftware.InnoSetup`); the release job installs it with Chocolatey when the image lacks it.

## macOS
Experimental: a native arm64 build for Apple silicon and macOS 13.3 or newer; Intel Macs are not supported. The disk image
holds `OpenBeach.app`, a link to `/Applications` (the player drags the app onto it), the READMEs and `AGENTS.md`:

| Path in the app | What it is |
|---|---|
| `Contents/Info.plist` | `tools/ci/release/macos/Info.plist.in`; `CFBundleShortVersionString` is the version up to three numbers |
| `Contents/MacOS/openbeach-launcher` | the bundle's executable, a bash 3.2 script (`tools/ci/release/macos/`) |
| `Contents/MacOS/OpenBeach`, `obl-convert` | the game and the converter (stripped) |
| `Contents/Resources/import.sh` | the import, like Linux's, with `hdiutil attach` for a disc image |
| `Contents/Resources/l0/packs/l0/` | the demo's L0 pack |
| `Contents/Resources/openbeach.icns`, `LICENSE`, `THIRD_PARTY_NOTICES.md` | the icon (the 256 px PNG as `ic08`) and the licences |

**The game never writes into the app**, which would break its signature and fails on the read-only image. Its game
directories are `~/Library/Application Support/OpenBeach/userdata/` (the player's copy) and `.../l0/` (the demo, the
pack copied there at each start), and the launcher passes them with `--game-dir`. The game's own lookup of `obl-convert`
(old saves) takes the executable's directory there (`_NSGetExecutablePath`): in a bundle `SDL_GetBasePath` names
`Contents/Resources`.

**The launcher** starts the game on `userdata/` when it holds `packs/base/pack.json`. Otherwise an `osascript` dialog
offers the import or the demo. The import asks for the copy (`choose file`: a disc image is the copy, any other file names
its folder, so `BeachLife.exe` or a file on the CD works) and the language (`choose from list`, preselected from
`AppleLanguages`), then opens `import.command` (beside `userdata/`) in Terminal: `import.sh --prefer-language <code>
<copy>`, then `open` of the app, so the game starts after a successful import and a failure stays readable. `--demo`
starts the demo (`open -a OpenBeach --args --demo`).

**Signing.** There is no Developer ID: `codesign --sign -` signs the bundle ad hoc (arm64 runs no unsigned code), so
Gatekeeper refuses the first start of a downloaded copy and the player allows it once in System Settings, Privacy &
Security, "Open Anyway" (README). A Developer ID and notarization would remove that step.

## Icons
The OpenBeach mark (`assets/logo/openbeach-mark.svg`) is the program's icon everywhere: `tools/make_icons.py` renders
`assets/logo/openbeach.ico` (16 to 256 px, uncompressed 32-bit entries) and `openbeach-256.png`, both committed.
- **Window:** `cmake/embedded_icon.cmake` builds the .ico into the game (`openbeach_icon.gen.h`) and `SetGameIcon`
  (`src/platform/sdl_present.cpp`) sets it with `core/cursor_file.h` `DecodeIcon`, on every system and with every pack.
  The icon of the player's copy (`remaster/icon.ico` of `obl-convert resources`) is no longer used.
- **Windows file:** `src/launcher/openbeach.rc.in` makes the same .ico the first icon resource of `OpenBeach.exe`
  (Explorer, the taskbar, shortcuts).
- **Version resource:** `obl_version_resource` (`cmake/version_resource.cmake`, `cmake/version.rc.in`) gives `OpenBeach.exe`
  and `obl-convert.exe` the version of `version.txt` (Properties > Details); a bump of `version.txt` reconfigures.
- **Linux desktop:** an ELF file has no icon of its own, so `import.sh` adds `~/.local/share/applications/openbeach.desktop`
  with `game/openbeach.png` in the icon theme directory. The game sets the SDL app id `openbeach` (Wayland `app_id`, X11
  `WM_CLASS`), which ties its window to that entry for the dock and the task switcher.

## Build
**macOS** (`tools/ci/macos_release.sh`): `tools/obl_sim_linux.sh` with Apple Clang, `RelWithDebInfo`,
`CMAKE_OSX_ARCHITECTURES=arm64` and `CMAKE_OSX_DEPLOYMENT_TARGET=13.3`. Without the x86 flags the float math is scalar
IEEE with no FMA contraction (`-ffp-contract=off`), and the optimized `obl-sim` of the same build runs the same L0
references as Linux before anything is packed. SDL builds its Cocoa video, Core Audio and Metal/OpenGL renderers.
Then `strip -x`, `stage.py --platform macos`, the ad-hoc signature and `hdiutil create -format UDZO`. The job runs on
`macos-15` (Apple silicon).

**Windows** (`tools/ci/windows_release.ps1`): the `x64` preset with `-DOBL_CONVERT_ON_BUILD=OFF`, RelWithDebInfo, the
static MSVC runtime of every build, so the zip needs no redistributable; Inno Setup 6 builds the installer.
`tools/runtests.ps1 -Pack public -Config
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
  then stops the host. The job also installs with the installer (`/VERYSILENT`, no `/ISO`) into `build/installed` and runs
  the same test there.
- **Linux:** `tools/ci/linux_smoke.sh <unpacked tarball>` starts `game/OpenBeach` the same way under Xvfb (never a real
  display) with SDL's X11 driver, then sends `SIGTERM`; SDL turns it into its quit event, and the test expects `WinMain
  returned 0`, `process exit` and exit code 0. `tools/ci/linux_smoke_distros.sh` runs it in clean `debian:12` and
  `ubuntu:24.04` containers with only the README's packages (its English section, the first list) and Xvfb. The job
  keeps a screenshot of the Xvfb screen in its artifact.

- **macOS:** `tools/ci/macos_smoke.sh <OpenBeach.app>` runs on the app in the mounted disk image: `openbeach-launcher
  --demo` under a temporary `HOME`, the same log checks in a Cocoa window, `SIGTERM` and a clean exit; the job keeps a
  `screencapture` of the screen. `codesign --verify --deep --strict` checks the packed app's signature first.

Not covered: the import scripts and the installer's import (CI has no copy of the game), the macOS launcher's dialogs,
Gatekeeper on a downloaded copy, real input, a real audio device, Wayland and a GPU driver.

## Testing without a release
Run the workflow by hand (`gh workflow run release.yml --ref <branch>`): the `release` job is skipped, and `linux`,
`windows` and `macos` build, test and pack, then keep the archives (and the installer) as the workflow artifacts
`linux-release`, `windows-release` and `macos-release` (14 days) with the version `<version.txt>-dev.<commit>`. Only a release run attaches them to the release
(`gh release upload <tag> --clobber`). Locally: `./tools/ci/windows_release.ps1 [-SkipTests]`, and in WSL
`bash tools/ci/linux_release.sh` (an LF copy of the checkout, [../testing/unit-tests.md](../testing/unit-tests.md)), and on a
Mac with Xcode's command line tools, CMake and Ninja `bash tools/ci/macos_release.sh` (`OBL_SKIP_TESTS=1` skips the L0
references).
