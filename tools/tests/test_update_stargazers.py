"""The stargazers picture (tools/update_stargazers.py) from synthetic logins; no network."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from update_stargazers import LINE_CHARS, SHOWN, lines_for, render


class RenderTest(unittest.TestCase):
    def test_no_stars_invites_the_first(self):
        self.assertIn("be the first to star", render([]))

    def test_newest_come_first(self):
        text = " ".join(lines_for(["old", "mid", "new"]))
        self.assertLess(text.index("new"), text.index("mid"))
        self.assertLess(text.index("mid"), text.index("old"))
        self.assertNotIn(" more", text)

    def test_only_the_newest_are_named(self):
        text = " ".join(lines_for([f"user{i}" for i in range(SHOWN + 5)]))
        self.assertIn("user%d," % (SHOWN + 4), text)
        self.assertNotIn("user0,", text)
        self.assertIn("and 5 more", text)

    def test_long_lists_wrap(self):
        lines = lines_for([f"a-rather-long-login-{i}" for i in range(SHOWN)])
        self.assertGreater(len(lines), 1)
        self.assertTrue(all(len(line) <= LINE_CHARS for line in lines))

    def test_names_are_escaped(self):
        self.assertNotIn("<script", render(["<script>"]))

    def test_same_logins_give_the_same_file(self):
        self.assertEqual(render(["a", "b"]), render(["a", "b"]))


if __name__ == "__main__":
    unittest.main()
