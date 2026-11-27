import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ci_changes import is_documentation, needs_code_checks


class DocumentationTests(unittest.TestCase):
    def test_top_level_markdown_and_docs_are_documentation(self):
        for path in ("README.md", "CHANGELOG.md", "LICENSE", "docs/index.md", "docs/build/ci.md", "docs/modding/guides/publishing.md",
                     "docs\\modding\\catalogs.md"):
            self.assertTrue(is_documentation(path), path)

    def test_docs_the_tests_read_and_everything_else_are_code(self):
        for path in ("docs/modding/reference/actions.md", "docs/modding/examples/mod_template/mod.json", "data/mods/frame_counter/README.md",
                     "tests/l0_mod_catalog.txt", "src/game/mods/mod_catalog.cpp", ".github/workflows/ci.yml", "web/index.html", "version.txt"):
            self.assertFalse(is_documentation(path), path)

    def test_any_code_or_no_paths_needs_the_checks(self):
        self.assertFalse(needs_code_checks(["README.md", "docs/index.md"]))
        self.assertTrue(needs_code_checks(["README.md", "src/core/strings.h"]))
        self.assertTrue(needs_code_checks([]))


if __name__ == "__main__":
    unittest.main()
