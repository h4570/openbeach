"""Own fonts regenerate exactly and draw every character in the L0 text table."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))
from tools.gen_l0_pack import FONTS, write_fonts


class L0Fonts(unittest.TestCase):
    def test_generated_fonts(self):
        pack = REPO / "data/packs/l0"
        texts = json.loads((pack / "text/en.json").read_text(encoding="utf-8"))
        characters = set("0123456789:$%+-/.,")
        for _, value in texts["entries"]:
            characters.update(value)
        with tempfile.TemporaryDirectory() as directory:
            write_fonts(Path(directory))
            for name in FONTS:
                with self.subTest(font=name):
                    for suffix in ("json", "png"):
                        filename = f"{name}.{suffix}"
                        self.assertEqual((Path(directory) / "fonts" / filename).read_bytes(),
                                         (pack / "fonts" / filename).read_bytes())
                    doc = json.loads((pack / "fonts" / f"{name}.json").read_text())
                    with Image.open(pack / "fonts" / doc["atlas"]) as atlas:
                        atlas.load()
                        self.assertEqual(atlas.mode, "LA")
                        pixels = atlas.tobytes()
                        self.assertTrue(all(v <= 3 and a == (255 if v else 0)
                                            for v, a in zip(pixels[::2], pixels[1::2])))
                        crops = {}
                        for char in characters:
                            rect = doc["glyphs"][f"{ord(char):04x}"]
                            self.assertGreater(rect[2], rect[0])
                            glyph = atlas.crop(rect).getchannel("A")
                            self.assertEqual(glyph.getbbox() is None, char.isspace())
                            crops[char] = glyph.tobytes()
                        # Distinct silhouettes catch a return to generic boxes or fallback glyphs.
                        for char in characters - set(" ?"):
                            self.assertNotEqual(crops[char], atlas.crop(
                                doc["glyphs"]["003f"]).getchannel("A").tobytes())
                        capitals = [atlas.crop(doc["glyphs"][f"{ord(c):04x}"])
                                    .getchannel("A").tobytes() for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"]
                        self.assertEqual(len(set(capitals)), 36)


if __name__ == "__main__":
    unittest.main()
