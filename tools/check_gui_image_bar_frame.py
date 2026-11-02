"""Compare the real GuiImage/GuiBar fixture frame with its committed digest.

Arguments: the game directory and optionally the fixture name (default gui_image_bar_frame.json, the private digest).
"""
import hashlib
import json
from pathlib import Path
import sys


def main():
    fixture = Path(__file__).resolve().parents[1] / "tests" / (sys.argv[2] if len(sys.argv) > 2 else "gui_image_bar_frame.json")
    expected = json.loads(fixture.read_text(encoding="utf-8"))["sha256"]
    path = Path(sys.argv[1]) / "captures" / "gui_image_bar_frame.png"
    actual = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else "missing"
    match = actual == expected
    print(f"{'PASS' if match else 'FAIL'} GuiImage/GuiBar frame sha256={actual}")
    if not match:
        print(f"expected sha256={expected} box=whole PNG file {path}")
    return 0 if match else 1


if __name__ == "__main__":
    sys.exit(main())
