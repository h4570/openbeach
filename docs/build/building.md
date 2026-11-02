# Building

## Requirements
- Windows 10/11, Visual Studio 2026 with the C++ workload (its bundled CMake; the `Visual Studio 18 2026` generator needs
  CMake 4.2 or newer, older CMake on `PATH` will not do).
- Python 3 for the tools, PowerShell 7 (`pwsh`) for the `*.ps1` scripts.
- Dependencies (SDL3, nlohmann_json, pl_mpeg, dr_mp3, zlib for the converter) are fetched by CMake `FetchContent`; nothing to install.
- **x64 is the only Windows build**; the WebAssembly job is the only 32-bit build ([ci.md](ci.md)).

## Commands (PowerShell 7)
```powershell
./build.ps1                  # x64 Debug
./build.ps1 -Config release  # RelWithDebInfo
./build.ps1 -Test            # Debug build, then the unit tests (ctest)
```
Outputs land in `build/x64/bin/<Config>/`:

| Binary | What it is |
|---|---|
| `OpenBeach.exe` | The game: all game code linked statically; starts a copy of itself as the host process that runs it. |
| `obl_unit_tests.exe` | Unit tests, no game ([../testing/unit-tests.md](../testing/unit-tests.md)). |
| `obl-convert.exe` | The converter from the user's copy to our data formats: one command per format, `import` for a whole pack, plus `identify` and `cabinet` ([../architecture/data-formats/converter/index.md](../architecture/data-formats/converter/index.md)). |
| `obl_imgdiff.exe` | PNG comparison tool for golden images ([../testing/golden-images.md](../testing/golden-images.md)). |

The build defines the floating-point contract explicitly (`/arch:AVX2 /fp:precise`, no FMA contraction);
see [../testing/determinism.md](../testing/determinism.md).

## Linux
x86-64 with Clang, CMake, Ninja and Python 3 ([../architecture/linux-host.md](../architecture/linux-host.md)). Outputs land in
`build/linux-sim-x64/bin/`.

```bash
OBL_SIM_SKIP_CONVERSION=1 bash tools/obl_sim_linux.sh                           # obl-sim (headless) and obl-convert
OBL_SIM_SKIP_CONVERSION=1 bash tools/obl_sim_linux.sh -DOBL_LINUX_WINDOWED=ON   # also the windowed OpenBeach
bash tools/ci/l0_sim_tests.sh                                                   # the L0 tests on obl-sim
bash tools/unit_linux.sh clang                                                  # the unit tests
```
`OBL_SIM_BUILD_TYPE` sets the CMake build type (default `Debug`); the game targets are `-O2` in every type. The windowed
build needs SDL's X11 or Wayland development packages for a real window (`tools/ci/linux_release_packages.sh` lists them).
It starts, presents frames under Xvfb and quits cleanly; the release tarball is built with
`bash tools/ci/linux_release.sh` ([linux-release.md](linux-release.md)).

## WebAssembly
`bash tools/wasm_linux.sh` builds the unit tests, the simulation and the importer adapter with Emscripten (`EMSDK` names the SDK).

## Formatting
C++ is formatted with clang-format 21 (`python tools/format.py`, `--check` before commits);
[../workflow/code-style.md](../workflow/code-style.md).
