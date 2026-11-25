"""The bot and chaos run files with mods name the combined set of tests/l0_mods_combined.txt (docs/modding/testing.md#bots-and-chaos)."""
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import sim_workers  # noqa: E402


class ModRunFiles(unittest.TestCase):
    def test_header_env_reads_the_env_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.bot"
            path.write_text("# @env OBL_PACK=l0\n# note\n  #  @env OBL_MODS=a,b \nOBL-bot 1\n", encoding="utf-8")
            self.assertEqual({"OBL_PACK": "l0", "OBL_MODS": "a,b"}, sim_workers.header_env(path))
            self.assertEqual({}, sim_workers.header_env(Path(directory) / "missing.bot"))

    def test_run_files_use_the_combined_set(self):
        combined = sim_workers.header_env(ROOT / "tests/l0_mods_combined.txt")["OBL_MODS"]
        for run in ("tests/bots/l0/build_tree_mods.bot", "tests/chaos/l0_mods.chaos"):
            self.assertEqual(combined, sim_workers.header_env(ROOT / run)["OBL_MODS"], run)
        self.assertEqual("l0", sim_workers.header_env(ROOT / "tests/chaos/l0_mods.chaos")["OBL_PACK"])


if __name__ == "__main__":
    unittest.main()
