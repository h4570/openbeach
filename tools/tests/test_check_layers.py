"""Synthetic trees for tools/check_layers.py: a clean file and each kind of violation."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_layers import check


def tree(files):
    directory = tempfile.TemporaryDirectory()
    root = Path(directory.name)
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return directory, root


class LayerTests(unittest.TestCase):
    def problems(self, files):
        directory, root = tree(files)
        with directory:
            return check(root)

    def test_clean_files_pass(self):
        self.assertEqual(self.problems({
            "src/core/a.h": "int a;\n",
            "src/game/b.h": '#include "core/a.h"\n',
            "src/platform/c.cpp": '#include "game/b.h"\n',
            "src/formats/d.h": '#include "core/a.h"\n',
            "src/testing/e.cpp": '#include "importer/x.h"\n#include "formats/d.h"\n',
            "importer/x.h": "int x;\n"}), [])

    def test_violations(self):
        cases = {
            "Windows": {"src/game/a.cpp": "#include <windows.h>\n"},
            "boundary": {"src/game/a.h": '#include "common/layout/world.h"\n', "src/common/layout/world.h": "int w;\n"},
            "upward": {"src/core/a.h": '#include "game/b.h"\n', "src/game/b.h": "int b;\n"},
            "Windows header": {"src/testing/a.cpp": "#include <windows.h>\n"},
            "upward include": {"src/platform/a.cpp": '#include "testing/b.h"\n', "src/testing/b.h": "int b;\n"},
            "formats/a.h: includes src/game": {"src/formats/a.h": '#include "game/b.h"\n', "src/game/b.h": "int b;\n"},
            "includes the importer": {"src/game/a.cpp": '#include "importer/b.h"\n', "importer/b.h": "int b;\n"},
            "audited reach": {"importer/a.cpp": '#include "game/world/b.h"\n', "src/game/world/b.h": "int b;\n"},
            "names game implementation (obl_common)": {"importer/CMakeLists.txt": "target_link_libraries(x PUBLIC obl_common)\n"},
            "names game implementation (src/game/a.cpp)": {"importer/portable_sources.cmake": "list(APPEND X ${REPO}/src/game/a.cpp)\n"},
            "links the importer": {"src/game/CMakeLists.txt": "target_link_libraries(obl_common PUBLIC obl_convert)\n"},
        }
        for needle, files in cases.items():
            with self.subTest(needle):
                self.assertTrue(any(needle in p for p in self.problems(files)))

    def test_importer_reaches_audited_headers(self):
        self.assertEqual(self.problems({
            "importer/a.cpp": '#include "game/objects/guest_sets.h"\n#include "core/b.h"\n#include "formats/c.h"\n',
            "importer/CMakeLists.txt": "# never obl_common\ntarget_link_libraries(x PUBLIC obl_core)\n",
            "src/game/objects/guest_sets.h": "int a;\n", "src/core/b.h": "int b;\n", "src/formats/c.h": "int c;\n"}), [])

    def test_transitive_boundary(self):
        found = self.problems({"src/game/a.h": '#include "common/mid.h"\n', "src/common/mid.h": '#include "common/layout/record.h"\n',
                               "src/common/layout/record.h": "int g;\n"})
        self.assertEqual(len(found), 1)
        self.assertIn("mid.h", found[0])

    def test_windows_allowed_on_platform(self):
        self.assertEqual(self.problems({"src/platform/a.cpp": "#include <windows.h>\n"}), [])

    def test_shells_include_testing(self):
        self.assertEqual(self.problems({"src/dll/a.cpp": '#include "testing/b.h"\n', "src/testing/b.h": '#include "platform/c.h"\n',
                                        "src/platform/c.h": "int c;\n"}), [])


if __name__ == "__main__":
    unittest.main()
