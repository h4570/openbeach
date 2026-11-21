#!/usr/bin/env python3
"""Structural smoke check of a built OpenBeach Android APK (docs/build/releases.md).

    python3 tools/ci/android_smoke.py <path/to/openbeach.apk> [--version <expected>]

Verifies:
- APK contents: classes.dex, lib/arm64-v8a/libmain.so, L0 demo pack, LICENSE, THIRD_PARTY_NOTICES.md, no game data;
- ELF64 header of libmain.so: AArch64 (EM_AARCH64) and PT_LOAD segments aligned to 16 KB (Android 15+);
- SDK tool checks (when ANDROID_HOME is available): apksigner verify, zipalign -c, aapt2 dump badging.
"""
import argparse
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import obl_android

EM_AARCH64 = 183
PT_LOAD = 1
PAGE_16K = 16384
REQUIRED_ENTRIES = (
    "AndroidManifest.xml",
    "classes.dex",
    "lib/arm64-v8a/libmain.so",
    "assets/l0/packs/l0/pack.json",
    "assets/LICENSE",
    "assets/THIRD_PARTY_NOTICES.md",
)
FORBIDDEN_PREFIXES = ("assets/packs/base/", "assets/userdata/")


def check_elf64_arm64_16k(data):
    if len(data) < 64 or data[:4] != b"\x7fELF":
        raise ValueError("libmain.so is not an ELF binary")
    if data[4] != 2 or data[5] != 1:
        raise ValueError("libmain.so is not 64-bit little-endian ELF")
    e_machine = struct.unpack_from("<H", data, 18)[0]
    if e_machine != EM_AARCH64:
        raise ValueError(f"libmain.so machine {e_machine} != EM_AARCH64 ({EM_AARCH64})")
    e_phoff = struct.unpack_from("<Q", data, 32)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 54)
    loads = 0
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type = struct.unpack_from("<I", data, off)[0]
        if p_type == PT_LOAD:
            loads += 1
            p_align = struct.unpack_from("<Q", data, off + 48)[0]
            if p_align < PAGE_16K:
                raise ValueError(f"PT_LOAD segment {i} has p_align={p_align} (< {PAGE_16K})")
    if loads == 0:
        raise ValueError("libmain.so has no PT_LOAD segments")


def check_apk_contents(apk_path):
    with zipfile.ZipFile(apk_path, "r") as z:
        names = set(z.namelist())
        missing = [entry for entry in REQUIRED_ENTRIES if entry not in names]
        if missing:
            raise ValueError(f"APK missing required entries: {', '.join(missing)}")
        forbidden = [name for name in names if name.startswith(FORBIDDEN_PREFIXES)]
        if forbidden:
            raise ValueError(f"APK contains forbidden game data: {forbidden[0]}")
        check_elf64_arm64_16k(z.read("lib/arm64-v8a/libmain.so"))


def check_sdk_tools(apk_path, expected_version):
    sdk = obl_android.sdk_root()
    tools = obl_android.newest(sdk / "build-tools")
    subprocess.run([obl_android.tool(tools, "apksigner"), "verify", "--verbose", str(apk_path)], check=True)
    subprocess.run([obl_android.tool(tools, "zipalign"), "-c", "-p", "4", str(apk_path)], check=True)
    badging = subprocess.run(
        [obl_android.tool(tools, "aapt2"), "dump", "badging", str(apk_path)],
        check=True, capture_output=True, text=True, encoding="utf-8",
    ).stdout
    checks = [
        f"name='{obl_android.PACKAGE}'",
        f"sdkVersion:'{obl_android.MIN_SDK}'",
        f"targetSdkVersion:'{obl_android.TARGET_SDK}'",
        "native-code: 'arm64-v8a'",
        f"launchable-activity: name='{obl_android.PACKAGE}.LauncherActivity'",
    ]
    if expected_version:
        name, code = obl_android.parse_version(expected_version)
        checks.extend([f"versionCode='{code}'", f"versionName='{name}'"])
    for needle in checks:
        if needle not in badging:
            raise ValueError(f"aapt2 dump badging missing {needle!r}\n{badging}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--version", help="expected versionName (and derived versionCode)")
    parser.add_argument("--no-sdk", action="store_true", help="skip apksigner/zipalign/aapt2 checks")
    args = parser.parse_args()
    check_apk_contents(args.apk)
    if not args.no_sdk:
        check_sdk_tools(args.apk, args.version)
    print(f"android smoke: OK ({args.apk.name})")


if __name__ == "__main__":
    main()
