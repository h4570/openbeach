#!/usr/bin/env python3
"""Lays out a release directory (docs/build/releases.md) and, for Windows, packs it as a .zip.

    stage.py --platform windows|linux|macos --version <v> --out <dir>/<name> --bin <file>... [--zip <file.zip>]

The directory gets README.<language>.txt files and AGENTS.md (rendered from the templates beside this script: a line starting with
"[win]", "[linux]" or "[mac]", or with several of them as "[win|linux]", is kept only on those platforms). Windows and Linux
get game/ with the binaries, the platform's scripts, the L0 pack in game/l0/packs/l0/, an empty game/userdata/, the
licences and (Linux) the icon of the menu entry; macOS gets OpenBeach.app (the binaries and the launcher in
Contents/MacOS, the import script, the L0 pack, the icon and the licences in Contents/Resources) and the link to
/Applications that the disk image shows. Paths in the README files use the platform's separator.
"""

import argparse
import os
import re
import struct
import shutil
import stat
import sys
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PLATFORMS = {
    "windows": ("win", "Windows x64"),
    "linux": ("linux", "Linux x86-64"),
    "macos": ("mac", "macOS on Apple silicon"),
}
# Every tag a template may use; a line with another one is a typo, not another platform.
TAG = re.compile(r"^\[((?:win|linux|mac)(?:\|(?:win|linux|mac))*)\]")
APP = "OpenBeach.app"


def render(template, platform, version):
    tag, title = PLATFORMS[platform]
    lines = []
    for line in template.read_text(encoding="utf-8").splitlines():
        match = TAG.match(line)
        if match:
            if tag not in match.group(1).split("|"):
                continue
            line = line[match.end():]
        lines.append(line)
    heading = f"OpenBeach {version} for {title}"
    text = "\n".join(lines) + "\n"
    return text.replace("@VERSION@", version).replace("@PLATFORM@", title).replace("@UNDERLINE@", "=" * len(heading))


def windows_paths(text):
    # game/userdata/SaveGames/ -> game\userdata\SaveGames\ (only paths into the archive, not URLs).
    return re.sub(r"(?<![\w/.:])game/[\w./-]*", lambda m: m.group(0).replace("/", "\\"), text)


def write_text(path, text, platform):
    if platform == "windows":
        text = text.replace("\n", "\r\n")
    path.write_bytes(text.encode("utf-8"))


def bundle_version(version):
    # CFBundleShortVersionString takes up to three numbers: 1.11.0-dev.abc1234 is 1.11.0 there (the full one is in
    # CFBundleGetInfoString and the README).
    match = re.match(r"\d+(?:\.\d+){0,2}", version)
    return match.group(0) if match else "0"


def write_icns(png, target):
    # An .icns of one entry, the 256 px PNG ("ic08"): Finder and the Dock scale it to the smaller sizes.
    data = png.read_bytes()
    entry = b"ic08" + struct.pack(">I", 8 + len(data)) + data
    target.write_bytes(b"icns" + struct.pack(">I", 8 + len(entry)) + entry)


def make_executable(path):
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def stage_macos(version, out, binaries):
    contents = out / APP / "Contents"
    macos, resources = contents / "MacOS", contents / "Resources"
    resources.mkdir(parents=True)
    macos.mkdir()
    for binary in binaries:
        shutil.copy2(binary, macos / Path(binary).name)
    scripts = HERE / "macos"
    shutil.copyfile(scripts / "openbeach-launcher", macos / "openbeach-launcher")
    shutil.copyfile(scripts / "import.sh", resources / "import.sh")
    for script in (macos / "openbeach-launcher", resources / "import.sh"):
        make_executable(script)
    plist = (scripts / "Info.plist.in").read_text(encoding="utf-8")
    plist = plist.replace("@BUNDLE_VERSION@", bundle_version(version)).replace("@VERSION@", version)
    (contents / "Info.plist").write_text(plist, encoding="utf-8")
    write_icns(ROOT / "assets" / "logo" / "openbeach-256.png", resources / "openbeach.icns")
    shutil.copytree(ROOT / "data" / "packs" / "l0", resources / "l0" / "packs" / "l0")
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copyfile(ROOT / name, resources / name)
    # The disk image shows the app beside a link to /Applications, so the player drags one onto the other.
    os.symlink("/Applications", out / "Applications")


def stage(platform, version, out, binaries):
    if out.is_symlink() or out.is_file():
        out.unlink()
    elif out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for template in sorted(HERE.glob("README.*.txt.in")):
        readme = render(template, platform, version)
        if platform == "windows":
            readme = windows_paths(readme)
        write_text(out / template.name.removesuffix(".in"), readme, platform)
    write_text(out / "AGENTS.md", render(HERE / "AGENTS.md.in", platform, version), platform)
    if platform == "macos":
        stage_macos(version, out, binaries)
        return
    game = out / "game"
    (game / "userdata").mkdir(parents=True)
    for binary in binaries:
        shutil.copy2(binary, game / Path(binary).name)
    scripts = HERE / platform
    for source in sorted(p for p in scripts.rglob("*") if p.is_file()):
        target = game / source.relative_to(scripts)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if source.suffix == ".sh":
            make_executable(target)
    shutil.copytree(ROOT / "data" / "packs" / "l0", game / "l0" / "packs" / "l0")
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copyfile(ROOT / name, game / name)
    if platform == "linux":
        # The menu entry's icon (import.sh); on Windows the executable carries it.
        shutil.copyfile(ROOT / "assets" / "logo" / "openbeach-256.png", game / "openbeach.png")


def pack_zip(out, archive, epoch):
    # Sorted entries with one timestamp, so the same tree gives the same archive; the empty userdata/ stays as an entry.
    stamp = time.gmtime(max(epoch, 315532800))[:6]
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(out.rglob("*")):
            name = path.relative_to(out.parent).as_posix() + ("/" if path.is_dir() else "")
            if path.is_dir() and any(path.iterdir()):
                continue
            info = zipfile.ZipInfo(name, stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o40755 << 16 | 0x10) if path.is_dir() else (0o100644 << 16)
            z.writestr(info, b"" if path.is_dir() else path.read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--platform", choices=sorted(PLATFORMS), required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--bin", nargs="+", required=True)
    parser.add_argument("--zip", type=Path)
    parser.add_argument("--epoch", type=int, default=0, help="timestamp of the zip entries (SOURCE_DATE_EPOCH)")
    args = parser.parse_args()
    missing = [b for b in args.bin if not Path(b).is_file()]
    if missing:
        sys.exit(f"error: missing binaries: {' '.join(missing)}")
    stage(args.platform, args.version, args.out, args.bin)
    if args.zip:
        pack_zip(args.out, args.zip, args.epoch)
    print(f"staged {args.out}")


if __name__ == "__main__":
    main()
