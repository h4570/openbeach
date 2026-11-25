#!/usr/bin/env python3
"""Tests for tools/make_sprite.py."""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
TOOL = REPO / "tools" / "make_sprite.py"


def make_png(path: Path, size: tuple[int, int], fill: tuple[int, int, int, int],
             accent: tuple[int, int, int, int] | None = None) -> Path:
    im = Image.new("RGBA", size, (0, 0, 0, 0))
    w, h = size
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            im.putpixel((x, y), fill)
    if accent is not None and w >= 4 and h >= 4:
        im.putpixel((w // 2, h // 2), accent)
    path.parent.mkdir(parents=True, exist_ok=True)
    im.save(path)
    return path


def run_tool(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, str(TOOL), *args], capture_output=True, text=True, check=False)


class MakeSpriteTest(unittest.TestCase):
    def test_building_sprite_known_pngs_produce_expected_obl_sprite(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            f0 = make_png(root / "in" / "d0.png", (8, 6), (220, 80, 40, 255), (255, 255, 180, 255))
            f1 = make_png(root / "in" / "d1.png", (8, 6), (40, 160, 220, 255))
            light = make_png(root / "in" / "light.png", (8, 6), (200, 200, 200, 255))
            out = root / "sprites"

            res = run_tool(
                "--name", "test_stand",
                "--kind", "building",
                "--directions", "2",
                "--hotspot", "-4", "-6",
                "--lightmap", str(light), str(light),
                "--out", str(out),
                "--frames", str(f0), str(f1),
            )
            self.assertEqual(res.returncode, 0, res.stderr)

            doc = json.loads((out / "test_stand.json").read_text(encoding="utf-8"))
            self.assertEqual(doc["format"], "obl.sprite")
            self.assertEqual(doc["version"], 1)
            self.assertEqual(doc["name"], "test_stand")
            self.assertEqual(doc["kind"], "building")
            self.assertEqual(doc["pixels"], "indexed")
            self.assertEqual(doc["palette_colors"], 256)
            self.assertEqual(doc["palette_count"], 1)
            self.assertEqual(doc["depth_extents"], [255, 255, 255, 255])
            self.assertEqual([a["plane"] for a in doc["animations"]], ["color", "depth", "light"])
            self.assertEqual(doc["animations"][0]["directions"], 2)
            self.assertEqual(doc["animations"][0]["frames"], 1)
            self.assertEqual(doc["animations"][0]["size"], [8, 6])
            self.assertEqual(doc["animations"][0]["hotspot"], [-4, -6])

            # Palette: RGB 256x1, entry 0 is (0, 0, 0), entries 1..3 are the 3 sorted colors.
            with Image.open(out / doc["palettes"]) as pal:
                self.assertEqual(pal.mode, "RGB")
                self.assertEqual(pal.size, (256, 1))
                self.assertEqual(pal.getpixel((0, 0)), (0, 0, 0))
                self.assertEqual(
                    [pal.getpixel((i, 0)) for i in (1, 2, 3)],
                    [(40, 160, 220), (220, 80, 40), (255, 255, 180)],
                )

            # Color and auto-depth sheets must be LA, 16x6, with identical alpha masks.
            with Image.open(out / doc["animations"][0]["sheets"][0]) as color_sheet, \
                 Image.open(out / doc["animations"][1]["sheets"][0]) as depth_sheet:
                self.assertEqual(color_sheet.mode, "LA")
                self.assertEqual(color_sheet.size, (16, 6))
                self.assertEqual(depth_sheet.mode, "LA")
                self.assertEqual(depth_sheet.size, (16, 6))
                self.assertEqual(color_sheet.getpixel((0, 0)), (0, 0))
                self.assertEqual(depth_sheet.getpixel((0, 0)), (0, 0))
                self.assertEqual(color_sheet.getpixel((4, 3)), (3, 255))
                self.assertEqual(color_sheet.getpixel((10, 2)), (1, 255))
                get_px = lambda im: im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()
                color_alpha = [a for _, a in get_px(color_sheet)]
                depth_alpha = [a for _, a in get_px(depth_sheet)]
                self.assertEqual(color_alpha, depth_alpha)

    def test_ui_icon_and_repeat_directions(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            icon = make_png(root / "icon.png", (48, 48), (60, 180, 120, 255))
            out = root / "sprites"
            res = run_tool("--name", "stand_icon", "--kind", "ui", "--out", str(out), "--frames", str(icon))
            self.assertEqual(res.returncode, 0, res.stderr)
            doc = json.loads((out / "stand_icon.json").read_text(encoding="utf-8"))
            self.assertEqual(doc["kind"], "ui")
            self.assertEqual(len(doc["animations"]), 1)
            self.assertEqual(doc["animations"][0]["hotspot"], [0, 0])
            self.assertEqual(doc["animations"][0]["size"], [48, 48])

            # Single building frame repeated across 4 directions uses frame_map.
            front = make_png(root / "front.png", (10, 10), (180, 90, 60, 255))
            res2 = run_tool(
                "--name", "one_view", "--kind", "building", "--directions", "4",
                "--repeat-directions", "--out", str(out), "--frames", str(front),
            )
            self.assertEqual(res2.returncode, 0, res2.stderr)
            doc2 = json.loads((out / "one_view.json").read_text(encoding="utf-8"))
            self.assertEqual(doc2["animations"][0]["unique_frames"], 1)
            self.assertEqual(doc2["animations"][0]["frame_map"], [0, 0, 0, 0])

    def test_bad_frame_size_is_refused_with_message(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            f0 = make_png(root / "a.png", (16, 16), (255, 0, 0, 255))
            f1 = make_png(root / "b.png", (20, 16), (0, 255, 0, 255))
            out = root / "sprites"
            res = run_tool("--name", "bad", "--kind", "ui", "--out", str(out), "--frames", str(f0), str(f1))
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("20x16", res.stderr)
            self.assertIn("16x16", res.stderr)
            self.assertFalse((out / "bad.json").exists())

    def test_bad_direction_count_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            f0 = make_png(root / "a.png", (8, 8), (255, 0, 0, 255))
            out = root / "sprites"
            res = run_tool("--name", "bad_dir", "--kind", "building", "--directions", "4",
                           "--out", str(out), "--frames", str(f0))
            self.assertNotEqual(res.returncode, 0)
            self.assertIn("4 directions", res.stderr)


if __name__ == "__main__":
    unittest.main()
