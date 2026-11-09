import plistlib
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ci" / "release"))
import stage


class RenderTests(unittest.TestCase):
    def test_tags_keep_lines_of_their_platforms(self):
        with tempfile.TemporaryDirectory() as tmp:
            template = Path(tmp) / "README.en.txt.in"
            template.write_text("all\n[win]win\n[linux]linux\n[mac]mac\n[win|linux]archive\n[linux|mac]unix\n",
                                encoding="utf-8")
            self.assertEqual(stage.render(template, "windows", "1.0").split("\n")[:-1], ["all", "win", "archive"])
            self.assertEqual(stage.render(template, "linux", "1.0").split("\n")[:-1], ["all", "linux", "archive", "unix"])
            self.assertEqual(stage.render(template, "macos", "1.0").split("\n")[:-1], ["all", "mac", "unix"])

    def test_every_template_line_tag_is_known(self):
        # A misspelt tag would print on every platform.
        for template in sorted(stage.HERE.glob("*.in")):
            for number, line in enumerate(template.read_text(encoding="utf-8").splitlines(), 1):
                if line.startswith("[") and not line.startswith("[!"):
                    self.assertTrue(stage.TAG.match(line), f"{template.name}:{number}: {line}")

    def test_bundle_version(self):
        self.assertEqual(stage.bundle_version("1.11.0"), "1.11.0")
        self.assertEqual(stage.bundle_version("1.11.0-dev.abc1234"), "1.11.0")
        self.assertEqual(stage.bundle_version("v2"), "0")


class MacLayoutTests(unittest.TestCase):
    def test_app_bundle(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            binaries = []
            for name in ("OpenBeach", "obl-convert"):
                (root / name).write_bytes(b"binary")
                binaries.append(root / name)
            out = root / "openbeach-1.2.3-macos-arm64"
            stage.stage("macos", "1.2.3-dev.abc", out, binaries)
            contents = out / "OpenBeach.app" / "Contents"
            for name in ("OpenBeach", "obl-convert", "openbeach-launcher"):
                self.assertTrue((contents / "MacOS" / name).is_file(), name)
            for script in (contents / "MacOS" / "openbeach-launcher", contents / "Resources" / "import.sh"):
                self.assertTrue(script.stat().st_mode & 0o111, script.name)
            self.assertTrue((contents / "Resources" / "l0" / "packs" / "l0" / "pack.json").is_file())
            self.assertTrue((contents / "Resources" / "LICENSE").is_file())
            self.assertEqual(str((out / "Applications").readlink()), "/Applications")
            self.assertTrue((out / "README.en.txt").is_file() and (out / "AGENTS.md").is_file())
            self.assertFalse((out / "game").exists())

            info = plistlib.loads((contents / "Info.plist").read_bytes())
            self.assertEqual(info["CFBundleExecutable"], "openbeach-launcher")
            self.assertEqual(info["CFBundleShortVersionString"], "1.2.3")
            self.assertEqual(info["CFBundleGetInfoString"], "OpenBeach 1.2.3-dev.abc")
            self.assertTrue((contents / "Resources" / (info["CFBundleIconFile"] + ".icns")).is_file())

            icns = (contents / "Resources" / "openbeach.icns").read_bytes()
            self.assertEqual(icns[:4], b"icns")
            self.assertEqual(struct.unpack(">I", icns[4:8])[0], len(icns))
            self.assertEqual(icns[8:12], b"ic08")
            self.assertEqual(icns[16:24], b"\x89PNG\r\n\x1a\n")

            readme = (out / "README.en.txt").read_text(encoding="utf-8")
            self.assertIn("for macOS on Apple silicon", readme)
            self.assertNotIn("game/", readme)
            self.assertNotIn("Play.cmd", readme)


if __name__ == "__main__":
    unittest.main()
