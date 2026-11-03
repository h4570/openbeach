#!/usr/bin/env python3
"""Lays out a release directory (docs/build/releases.md) and, for Windows, packs it as a .zip.

    stage.py --platform windows|linux --version <v> --out <dir>/<name> --bin <file>... [--zip <file.zip>]

The directory gets README.txt and AGENTS.md (rendered from the templates beside this script: a line starting with
"[win]" or "[linux]" is kept only on that platform), and game/ with the binaries, the platform's scripts, the L0 pack in
game/l0/packs/l0/, an empty game/userdata/ and the licences. Paths in README.txt use the platform's separator.
"""

import argparse
import re
import shutil
import stat
import sys
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PLATFORMS = {"windows": ("win", "Windows x64"), "linux": ("linux", "Linux x86-64")}
# Every tag a template may use; a line with another one is a typo, not a third platform.
TAG = re.compile(r"^\[(win|linux)\]")


def render(template, platform, version):
    tag, title = PLATFORMS[platform]
    lines = []
    for line in template.read_text(encoding="utf-8").splitlines():
        match = TAG.match(line)
        if match:
            if match.group(1) != tag:
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


def stage(platform, version, out, binaries):
    if out.exists():
        shutil.rmtree(out)
    game = out / "game"
    (game / "userdata").mkdir(parents=True)
    readme = render(HERE / "README.txt.in", platform, version)
    if platform == "windows":
        readme = windows_paths(readme)
    write_text(out / "README.txt", readme, platform)
    write_text(out / "AGENTS.md", render(HERE / "AGENTS.md.in", platform, version), platform)
    for binary in binaries:
        shutil.copy2(binary, game / Path(binary).name)
    scripts = HERE / platform
    for source in sorted(p for p in scripts.rglob("*") if p.is_file()):
        target = game / source.relative_to(scripts)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if source.suffix == ".sh":
            target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    shutil.copytree(ROOT / "data" / "packs" / "l0", game / "l0" / "packs" / "l0")
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copyfile(ROOT / name, game / name)


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
