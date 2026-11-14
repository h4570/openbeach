#!/usr/bin/env python3
"""Cross-builds the windowed OpenBeach for aarch64 handhelds (docs/build/handheld.md) with Zig as the C/C++ compiler.

    python tools/obl_handheld.py [--zig "<command>"] [--build-dir DIR] [--type Release] [-- extra cmake -D options]

Needs cmake, ninja and Zig (`pip install ziglang cmake ninja`, or Homebrew's zig), and from the device only its SDL2 and ALSA headers
and libasound.so (build/handheld-sysroot/, see --sysroot); no container: Zig carries the glibc headers and libraries of the target
version. The default target is glibc 2.35 (Ubuntu 22.04, the Anbernic firmware). Result: <build-dir>/bin/OpenBeach.
"""
import argparse
import os
import shlex
import shutil
import stat
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WINDOWS = os.name == "nt"


def zig_command(given):
    if given:
        return shlex.split(given, posix=not WINDOWS)
    if shutil.which("zig"):
        return ["zig"]
    return [sys.executable, "-m", "ziglang"]


def write_wrapper(directory, name, zig, args):
    """A one-file compiler for CMake, which takes a program path and not a command line."""
    command = " ".join(shlex.quote(part) if not WINDOWS else f'"{part}"' for part in zig + args)
    if WINDOWS:
        path = directory / f"{name}.cmd"
        path.write_text(f"@echo off\r\n{command} %*\r\n")
    else:
        path = directory / name
        path.write_text(f"#!/bin/sh\nexec {command} \"$@\"\n")
        path.chmod(path.stat().st_mode | stat.S_IEXEC)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--zig", help="the Zig command, if it is not `zig` or `python -m ziglang`")
    parser.add_argument("--build-dir", default=str(ROOT / "build" / "handheld-aarch64"))
    parser.add_argument("--type", default="Release")
    parser.add_argument("--glibc", default="2.35", help="the oldest glibc the program runs on")
    parser.add_argument("--sysroot", default=str(ROOT / "build" / "handheld-sysroot"),
                        help="the device's SDL2 and ALSA headers (include/SDL2, include/alsa) and libasound.so (lib/); see docs/build/handheld.md")
    parser.add_argument("--no-build", action="store_true", help="configure only")
    parser.add_argument("cmake_args", nargs="*")
    args = parser.parse_args()

    # A virtualenv with `pip install cmake ninja ziglang` keeps its tools beside the interpreter.
    os.environ["PATH"] = str(Path(sys.executable).parent) + os.pathsep + os.environ.get("PATH", "")
    build = Path(args.build_dir).resolve()
    tools = build / "zig-tools"
    tools.mkdir(parents=True, exist_ok=True)
    zig = zig_command(args.zig)
    target = f"aarch64-linux-gnu.{args.glibc}"
    cc = write_wrapper(tools, "zig-cc", zig, ["cc", "-target", target, "-Wno-error=date-time"])
    cxx = write_wrapper(tools, "zig-cxx", zig, ["c++", "-target", target, "-Wno-error=date-time"])
    ar = write_wrapper(tools, "zig-ar", zig, ["ar"])
    ranlib = write_wrapper(tools, "zig-ranlib", zig, ["ranlib"])

    sysroot = Path(args.sysroot)
    needed = [sysroot / "include" / "SDL2" / "SDL.h", sysroot / "include" / "alsa" / "asoundlib.h", sysroot / "lib" / "libasound.so"]
    missing = [str(path) for path in needed if not path.exists()]
    if missing:
        # The panel shows frames through the device's SDL2 and sets its ALSA volume; SDL3's audio uses the same ALSA.
        sys.exit("error: the sysroot lacks " + ", ".join(missing) + " (copy them from the device: docs/build/handheld.md)")
    device = [f"-DOBL_SDL2_INCLUDE_DIR={sysroot / 'include' / 'SDL2'}", f"-DALSA_INCLUDE_DIR={sysroot / 'include'}",
              f"-DALSA_LIBRARY={sysroot / 'lib' / 'libasound.so'}"]
    configure = [
        "cmake", "-S", str(ROOT / "src" / "host_linux"), "-B", str(build), "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={args.type}", "-DCMAKE_SYSTEM_NAME=Linux", "-DCMAKE_SYSTEM_PROCESSOR=aarch64",
        f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}", f"-DCMAKE_AR={ar}", f"-DCMAKE_RANLIB={ranlib}",
        "-DOBL_LINUX_WINDOWED=ON", "-DOBL_HANDHELD=ON",
        # Zig's linker does not take Clang's precompiled-header object, and the game logs its __DATE__ and __TIME__.
        "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON", "-DCMAKE_EXE_LINKER_FLAGS=-s",
        *device,
        # The host tools (importer, converter) are not built: the pack is converted on the PC.
        *args.cmake_args,
    ]
    subprocess.run(configure, check=True)
    if not args.no_build:
        subprocess.run(["cmake", "--build", str(build), "--target", "OpenBeach"], check=True)
        print(f"OpenBeach: {build / 'bin' / 'OpenBeach'}")


if __name__ == "__main__":
    main()
