"""Test the mod CI wrapper with a substitute engine."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_mod


class CheckModTests(unittest.TestCase):
    def test_wrapper_runs_engine_even_with_content_violations(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            mod = root / "source with spaces"
            mod.mkdir()
            (mod / "installed.dll").touch()
            args = ["check_mod", str(mod), "--game-dir", str(root), "--engine", "obl-sim", "--pack", "l0"]
            with patch.object(sys, "argv", args), patch.object(check_mod.subprocess, "run") as run:
                run.return_value = SimpleNamespace(returncode=0)
                self.assertEqual(check_mod.main(), 1)
                self.assertEqual(run.call_args.args[0], [
                    "obl-sim", "--game-dir", str(root.resolve()), "--check-mods", str(mod.resolve()),
                ])
                self.assertEqual(run.call_args.kwargs["env"]["OBL_PACK"], "l0")
                (mod / "installed.dll").unlink()
                self.assertEqual(check_mod.main(), 0)
                run.return_value = SimpleNamespace(returncode=8)
                self.assertEqual(check_mod.main(), 8)
