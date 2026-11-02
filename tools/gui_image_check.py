#!/usr/bin/env python3
"""The post-run oracle of tests/m6_gui_golden*.txt (docs/testing/golden-images.md): every `shot` of the script, in
<game dir>/captures, equals its reference pixel for pixel. Recording stays in tools/gui_image_golden.ps1.

    python tools/gui_image_check.py GAME_DIR [--pack private|l0] [--golden DIR]

Without --pack it follows OBL_PACK. With --golden the references come from DIR, not from tests/golden-images: the private
references recorded on another host. Exit code: 0 identical, 1 a missing or different image.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[1]
SHOT = re.compile(r"^shot (\S+)\s*$", re.MULTILINE)


def pixels(path: Path) -> tuple[tuple[int, int], bytes]:
    with Image.open(path) as image:
        return image.size, image.convert("RGB").tobytes()


def check(names: list[str], golden: Path, captures: Path) -> str:
    """The first problem, or "" when every image is identical."""
    for name in names:
        reference, candidate = golden / f"{name}.png", captures / f"{name}.png"
        if not reference.is_file():
            return f"missing GUI golden: {reference}"
        if not candidate.is_file():
            return f"missing GUI capture: {candidate}"
        try:
            if pixels(candidate) != pixels(reference):
                return f"GUI golden mismatch {name}: pixels differ"
        except OSError as error:
            return f"GUI golden mismatch {name}: {error}"
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game_dir", type=Path)
    parser.add_argument("--pack", choices=["private", "l0"])
    parser.add_argument("--golden", type=Path)
    args = parser.parse_args()
    # The outer -Record invocation owns the two-run comparison.
    if os.environ.get("OBL_GUI_ORACLE_RECORDING") == "1":
        return 0
    pack = args.pack or ("l0" if os.environ.get("OBL_PACK") == "l0" else "private")
    script, directory = ("m6_gui_golden_l0.txt", "m6-gui-l0") if pack == "l0" else ("m6_gui_golden.txt", "m6-gui")
    names = SHOT.findall((REPO / "tests" / script).read_text(encoding="utf-8"))
    if not names:
        print("GUI fixture has no images", file=sys.stderr)
        return 1
    problem = check(names, args.golden or REPO / "tests" / "golden-images" / directory, args.game_dir / "captures")
    if problem:
        print(problem, file=sys.stderr)
        return 1
    print(f"GUI golden: {len(names)} images, IDENTICAL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
