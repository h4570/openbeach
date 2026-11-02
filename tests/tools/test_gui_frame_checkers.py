"""A mismatching capture makes every GUI frame checker fail and name the pixels it compared."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
CASES = {
    "containers": ("gui_containers_combo", "box=(160, 165, 640, 440)"),
    "controls": ("gui_controls_menu", "box=whole PNG file"),
    "dialog": ("gui_dialog_staff", "boxes=((255, 105, 580, 225), (590, 92, 630, 120))"),
    "image_bar": ("gui_image_bar_frame", "box=whole PNG file"),
    "leaves": ("gui_leaves_menu", "box=whole PNG file"),
    "tabs": ("gui_tabs_staff", "boxes=((255, 105, 580, 225), (590, 92, 630, 120))"),
}


# The own fixture: the default one is a private digest.
ARGS = {"image_bar": ("gui_image_bar_frame_l0.json",)}


class MismatchReport(unittest.TestCase):
    def test_reports_coordinates(self):
        with tempfile.TemporaryDirectory() as temp:
            game = Path(temp)
            (game / "captures").mkdir()
            dump = {"open": [{"dialog": 1}], "widgets": [{"addr": 1, "rect": [0, 0, 1, 1], "base": [0, 0]}]}
            (game / "gui-dump.json").write_text(json.dumps(dump))
            for name, (capture, report) in CASES.items():
                checker = REPO / "tools" / f"check_gui_{name}_frame.py"
                if not checker.is_file():  # the exported tree keeps only the checkers of its own frames
                    continue
                Image.new("RGB", (800, 600)).save(game / "captures" / f"{capture}.png")
                with self.subTest(name):
                    run = subprocess.run(
                        [sys.executable, str(checker), str(game), *ARGS.get(name, ())],
                        capture_output=True, text=True,
                    )
                    self.assertEqual(run.returncode, 1, run.stdout + run.stderr)
                    self.assertIn(report, run.stdout)


if __name__ == "__main__":
    unittest.main()
