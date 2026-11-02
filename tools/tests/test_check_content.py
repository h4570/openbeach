import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_content import violations


def write_tree(root, files):
    for name, content in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content if isinstance(content, bytes) else content.encode("utf-8"))


class ContentTests(unittest.TestCase):
    def test_pack_members_and_standalone_markers(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_tree(root, {
                "pack/pack.json": '{"provenance":"converted"}',
                "pack/sprites/nested/image.png": "synthetic pixels",
                "gui.json": '{"provenance":"converted"}',
                "own/pack.json": '{"provenance":"own"}',
                "own/image.png": "own pixels",
                "source.cpp": 'auto json = R"({"provenance":"converted"})";',
                "broken.json": "invalid JSON",
            })
            # The manifest can be ignored/untracked while its payload was accidentally added.
            errors = violations(root, ["pack/sprites/nested/image.png", "gui.json", "own/pack.json",
                                       "own/image.png", "source.cpp", "broken.json", "deleted.json"])
            self.assertEqual(len(errors), 2)
            self.assertIn("converted pack", errors[0])
            self.assertIn("marker", errors[1])
            self.assertEqual(violations(root, ["pack/pack.json"])[0],
                             "pack/pack.json: converter provenance marker")

    def test_installation_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            files = {"BeachLife.exe": b"MZ\x90", "lib/x.DLL": b"data", "disc.iso": b"x", "renamed.dat": b"MZ\x90\x00",
                     "notes.txt": b"MZ is fine in text", "sound.wav": b"RIFF"}
            write_tree(root, files)
            errors = violations(root, list(files))
            self.assertEqual([e.split(":")[0] for e in errors], ["BeachLife.exe", "lib/x.DLL", "disc.iso", "renamed.dat"])

    def test_player_local_data_and_import_markers(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            files = {"run/BeachLife.ini": "x", "og-data/a.txt": "x", "saves/game.sav": "x", "web/import.zip": "x",
                     "web/profile/IndexedDB/db/000003.log": "x", "imp/pack.json": '{"provenance":"imported"}',
                     "imp/a.png": "x", "src/run/ok.cpp": "x", "data/packs/l0/pack.json": '{"provenance":"own"}'}
            write_tree(root, files)
            errors = violations(root, list(files))
            self.assertEqual([e.split(":")[0] for e in errors],
                             ["run/BeachLife.ini", "og-data/a.txt", "saves/game.sav", "web/import.zip",
                              "web/profile/IndexedDB/db/000003.log", "imp/pack.json", "imp/a.png"])


if __name__ == "__main__":
    unittest.main()
