# Third-party notices

OpenBeach is licensed under GPL-3.0-or-later ([LICENSE](LICENSE)). It uses the components below. Every licence is
compatible with GPL-3.0-or-later (permissive licences: MIT, zlib, public domain/MIT-0; Apache-2.0 with the LLVM
exception only for the Emscripten runtime libraries, which GPLv3 also accepts), so the combined work may be distributed
under GPL-3.0-or-later. The full licence text of each component is in its source tree or header, named below.

## Source included in this repository
| Component | Version | Licence | Where |
|---|---|---|---|
| [stb_image_write.h](https://github.com/nothings/stb) | v1.16 (commit `2c980bb`) | MIT or public domain (Unlicense), at your choice | `third_party/stb/`, the notice is at the end of the header |
| [stb_image.h](https://github.com/nothings/stb) | v2.30 (test oracle only) | MIT or public domain, at your choice | `third_party/stb/`, same notice |
| [fdlibm](https://www.netlib.org/fdlibm/) 5.3 (Sun Microsystems) | 5.3 | permissive Sun notice (free use, copy, modify, distribute, if the notice is kept; GPL-compatible) | `src/core/det_math.*`, ported `acos`, `sin`, `cos`, `rem_pio2`; the notice is at the top of the source |

## Fetched by the build (CMake `FetchContent`; not stored in this repository)
| Component | Version | Licence | Use |
|---|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | release-3.4.16 | zlib | window, input, audio and the Emscripten backend of the browser build; linked statically |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | MIT | JSON data, scenario and settings files |
| [zlib](https://github.com/madler/zlib) | v1.3.1 | zlib | inflate for pack and import code |
| [pl_mpeg](https://github.com/phoboslab/pl_mpeg) | commit `c871f2b` | MIT | MPEG video decoding (`pl_mpeg.h`) |
| [dr_mp3](https://github.com/mackron/dr_libs) | commit `dfe8377` | public domain or MIT-0, at your choice | MP3 decoding (`dr_mp3.h`) |

## Build and CI tools (not distributed with the game)
| Tool | Licence | Use |
|---|---|---|
| [Emscripten](https://github.com/emscripten-core/emscripten) / emsdk 6.0.13 | MIT (University of Illinois/NCSA for parts) | WebAssembly build; its runtime libraries (musl libc: MIT, libc++: Apache-2.0 with LLVM exception) are linked into the browser build |
| clang-format, clang-tidy, GCC, Clang, CMake, Ninja, ccache | Apache-2.0 with LLVM exception, GPL-3.0 with runtime library exception, BSD-3-Clause, GPL-3.0 | compile, format and lint only; no code of these tools is part of the game |
| [Pillow](https://github.com/python-pillow/Pillow) | MIT-CMU (HPND) | image comparison in the tool tests only |
| GitHub Actions (`actions/checkout`, `setup-python`, `cache`, `upload-artifact`) | MIT | CI workflow |

## Game data
The game assets are not part of OpenBeach. The player imports their own copy of the original game; nothing of it is
distributed here. The project's own L0 pack and scenarios are covered by the repository licence.
