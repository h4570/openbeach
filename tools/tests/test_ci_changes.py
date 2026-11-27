import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ci_changes import is_documentation, is_site, needs_code_checks


class DocumentationTests(unittest.TestCase):
    def test_top_level_markdown_and_docs_are_documentation(self):
        for path in ("README.md", "CHANGELOG.md", "LICENSE", "docs/index.md", "docs/build/ci.md", "docs/modding/guides/publishing.md",
                     "docs\\modding\\catalogs.md"):
            self.assertTrue(is_documentation(path), path)

    def test_site_files_and_assets_are_site(self):
        for path in ("web/index.html", "web/site_text.js", "web\\site.js", "assets/screenshots/night.jpg",
                     "assets/social/openbeach-featured-en.jpg", "assets/logo/favicon.ico"):
            self.assertTrue(is_site(path), path)

    def test_docs_the_tests_read_and_everything_else_are_code(self):
        for path in ("docs/modding/reference/actions.md", "docs/modding/examples/mod_template/mod.json", "data/mods/frame_counter/README.md",
                     "tests/l0_mod_catalog.txt", "src/game/mods/mod_catalog.cpp", ".github/workflows/ci.yml", "version.txt",
                     "importer/web/import_worker.js"):
            self.assertFalse(is_documentation(path), path)
            self.assertFalse(is_site(path), path)

    def test_any_code_or_no_paths_needs_the_checks(self):
        self.assertFalse(needs_code_checks(["README.md", "docs/index.md"]))
        self.assertFalse(needs_code_checks(["web/index.html", "web/site_text.js"]))
        self.assertFalse(needs_code_checks(["docs/index.md", "web/index.html", "assets/screenshots/night.jpg"]))
        self.assertTrue(needs_code_checks(["README.md", "src/core/strings.h"]))
        self.assertTrue(needs_code_checks(["web/index.html", "src/launcher/browser_files.js"]))
        self.assertTrue(needs_code_checks([]))


if __name__ == "__main__":
    unittest.main()
