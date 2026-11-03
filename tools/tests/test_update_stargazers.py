"""The stargazers block of the README (tools/update_stargazers.py) from synthetic logins; no network."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from update_stargazers import END, SHOWN, START, render, replace_block

REPO = "h4570/openbeach"


class RenderTest(unittest.TestCase):
    def test_no_stars_invites_the_first(self):
        self.assertIn("be the first to star", render([], REPO))

    def test_newest_come_first(self):
        block = render(["old", "mid", "new"], REPO)
        self.assertLess(block.index(">new<"), block.index(">mid<"))
        self.assertLess(block.index(">mid<"), block.index(">old<"))
        self.assertNotIn(" more", block)

    def test_only_the_newest_are_named(self):
        logins = [f"user{i}" for i in range(SHOWN + 5)]
        block = render(logins, REPO)
        self.assertIn(">user%d<" % (SHOWN + 4), block)
        self.assertNotIn(">user0<", block)
        self.assertIn(">5 more<", block)

    def test_names_are_escaped(self):
        self.assertNotIn("<script", render(["<script>"], REPO))


class ReplaceBlockTest(unittest.TestCase):
    TEXT = f"before\n{START}\nold\nlines\n{END}\nafter\n"

    def test_replaces_only_the_block(self):
        self.assertEqual(replace_block(self.TEXT, "new"), f"before\n{START}\nnew\n{END}\nafter\n")

    def test_same_block_changes_nothing(self):
        once = replace_block(self.TEXT, "new")
        self.assertEqual(replace_block(once, "new"), once)

    def test_missing_markers_are_an_error(self):
        with self.assertRaises(ValueError):
            replace_block("no markers here", "x")

    def test_block_text_is_taken_literally(self):
        self.assertIn(r"\1 \g<0>", replace_block(self.TEXT, r"\1 \g<0>"))


if __name__ == "__main__":
    unittest.main()
