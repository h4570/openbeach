import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_commits import message_problem, subject_problem


class SubjectTests(unittest.TestCase):
    def test_accepts_the_format(self):
        for subject in ("feat: publish OpenBeach", "fix(game): keep the queue after a reload", "feat(saves)!: drop the old header",
                        "docs: describe `run/`", "chore(main): release 1.0.0", "Merge branch 'x'", 'Revert "feat: a"'):
            self.assertIsNone(subject_problem(subject), subject)

    def test_refuses_other_forms(self):
        for subject in ("Add a thing", "feat add a thing", "feat(guests): add a thing", "feat: Add a thing", "feat: add a thing.",
                        "wip: add a thing", "feat: " + "a" * 70):
            self.assertIsNotNone(subject_problem(subject), subject)

    def test_the_pull_request_number_of_a_squash_merge_does_not_count(self):
        title = "feat: " + "a" * 66
        self.assertIsNone(subject_problem(title + " (#19)"))
        self.assertIsNotNone(subject_problem(title + "a (#19)"))

    def test_reads_only_the_subject_of_a_message(self):
        self.assertIsNone(message_problem("feat: add a thing\n\nA body of Any Form."))
        self.assertIsNone(message_problem("feat: add a thing\n\nWhy."))


if __name__ == "__main__":
    unittest.main()
