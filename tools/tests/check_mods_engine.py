"""Run the checker against the L0 failure and success fixtures on either desktop host."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/mods"


class ModChecker(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.game = Path(temporary.name)
        (self.game / "mods").mkdir()
        (self.game / "packs").mkdir()
        shutil.copytree(ROOT / "data/packs/l0", self.game / "packs/l0")

    def install(self, source):
        shutil.copytree(source, self.game / "mods" / source.name)
        return source.name

    def run_checker(self, active=(), source=None):
        env = dict(os.environ, OBL_PACK="l0", OBL_MODS=",".join(active))
        args = [str(ENGINE), "--game-dir", str(self.game), "--check-mods"]
        if source is not None:
            args.append(str(source))
        result = subprocess.run(args, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
        return result.returncode, result.stdout + result.stderr

    def test_all_failure_classes_in_one_run(self):
        active = [self.install(path) for path in sorted((FIXTURES / "broken").iterdir()) if path.is_dir()]
        active.extend(self.install(FIXTURES / name) for name in ("l0_conflict_a", "l0_conflict_b"))
        code, output = self.run_checker(active)
        self.assertNotEqual(code, 0, output)
        for message in (
            "bad_manifest", "version is not 1",
            "bad_scope", 'may not ship "rules/forbidden.txt"',
            "bad_requirement", 'requires "absent_mod"',
            "bad_pack", 'made for the pack "base"',
            "bad_api", "needs the presentation script API 999",
            "bad_setting", '"type" is not',
            "bad_action", "missing.action",
            "bad_data_patch", "/not/a/path", "/also/not/a/path",
            "bad_gui_patch", "absent_control",
            "bad_text_patch", "may only add keys",
            "bad_catalog_patch", "absent_record", "second_absent_record",
            "bad_vocabulary", "wrong_prefix",
            "bad_object_script", "invalid.obs",
            "bad_lua", "invalid.lua", "second.lua",
            "bad_lua_start", "absent_setting",
            "bad_lua_sandbox", "os is unavailable in the sandbox",
            "bad_lua_budget", "the instruction budget",
            "bad_lua_memory", "the memory cap",
            "bad_reference", "ABSENT_TEXT_KEY", "absent_building",
            "bad_schema", "bad_sprite.json",
            "l0_world_refused", "may not call ShowMenu ScenarioWon",
            "bad_recipe_schema", "obl.asset_recipe schema",
            "bad_recipe_operation", "crop",
            "bad_recipe_duplicate", "duplicate recipe output",
            'does not work with "l0_conflict_a"',
        ):
            with self.subTest(message=message):
                self.assertIn(message, output)

    def test_every_working_mod(self):
        fixtures = [p for p in FIXTURES.iterdir() if p.name != "broken" and (p / "mod.json").is_file()]
        fixtures.append(ROOT / "data/mods/frame_counter")
        fixtures.append(ROOT / "docs/modding/examples/mod_template")
        for source in sorted(fixtures):
            self.install(source)
        for source in sorted(fixtures):
            with self.subTest(mod=source.name):
                manifest = json.loads((source / "mod.json").read_text(encoding="utf-8"))
                code, output = self.run_checker([*manifest.get("requires", []), source.name])
                self.assertEqual(code, 0, output)
                if source.name == "l0_recipe":
                    self.assertIn('source "images/screens/not_in_l0.png" not found in pack', output)
                    self.assertFalse((self.game / "mod-cache").exists())

    def test_crlf_object_and_world_scripts(self):
        if not (FIXTURES / "l0_crlf").is_dir():
            self.skipTest("l0_crlf fixture is not on this base")
        mod = self.install(FIXTURES / "l0_crlf")
        scripts = self.game / "mods" / mod / "scripts"
        for name in ("object/busker.obs", "world/daily_bonus.wsc"):
            path = scripts / name
            path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        code, output = self.run_checker([mod])
        self.assertEqual(code, 0, output)

    def test_explicit_directory_ignores_active_settings(self):
        source = self.game / "source with spaces"
        shutil.copytree(ROOT / "data/mods/frame_counter", source)
        code, output = self.run_checker(["missing"], source)
        self.assertEqual(code, 0, output)
        self.assertFalse((self.game / "openbeach.ini").exists())
        self.assertFalse((self.game / "mod-data").exists())

    def test_conflicts(self):
        a, b = [self.install(FIXTURES / name) for name in ("l0_conflict_a", "l0_conflict_b")]
        code, output = self.run_checker([a, b])
        self.assertNotEqual(code, 0, output)
        self.assertIn('does not work with "l0_conflict_a"', output)
        a, b = [self.install(FIXTURES / name) for name in ("l0_takeover_a", "l0_takeover_b")]
        code, output = self.run_checker([a, b])
        self.assertEqual(code, 0, output)
        self.assertIn('"l0_takeover_b" wins', output)

    def test_conflict_winner_follows_load_order(self):
        a, b = [self.install(FIXTURES / name) for name in ("l0_takeover_a", "l0_takeover_b")]
        for order, winner in (([a, b], b), ([b, a], a)):
            with self.subTest(order=order):
                code, output = self.run_checker(order)
                self.assertEqual(code, 0, output)
                self.assertIn(f'"{winner}" wins', output)

    def test_same_state_name_is_refused_naming_both_mods(self):
        mods = [self.install(FIXTURES / name) for name in ("l0_busker", "l0_crlf")]
        code, output = self.run_checker(mods)
        self.assertNotEqual(code, 0, output)
        self.assertIn("STATE_BUSKER_PERFORM", output)
        self.assertIn("l0_busker", output)
        self.assertIn("l0_crlf", output)

    def test_combined_set_loads_together(self):
        text = (ROOT / "tests/l0_mods_combined.txt").read_text(encoding="utf-8")
        active = next(line for line in text.splitlines() if line.startswith("# @env OBL_MODS=")).split("=", 1)[1].split(",")
        for name in active:
            self.install(FIXTURES / name)
        code, output = self.run_checker(active)
        self.assertEqual(code, 0, output)

    def test_no_active_mods(self):
        code, output = self.run_checker()
        self.assertEqual(code, 0, output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    ENGINE = parser.parse_args().engine.resolve()
    unittest.main(argv=[__file__])
