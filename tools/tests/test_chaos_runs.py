"""Chaos runner (tools/chaos_runs.py): run file variants, replay parts and the shrinker against a fake host check; no game data needed."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import chaos_runs
from chaos_runs import Result, read_keys, replay_parts, signature, variant, write_commands

REPLAY = """OBL-replay 1
start scenario sandbox1
seed 3
fixed-dt 0.02
hash-schema 2
@0 hash 0123456789abcdef world=00000000/1
@0 set-paused paused=1
@10 dozer x=1 y=2
@20 hash fedcba9876543210
@20 demolish object=7
@30 hire-staff profession=Cleaner
end 100
"""


class ChaosRunsTest(unittest.TestCase):
    def test_soft_findings_are_visible_and_never_fail_or_shrink(self):
        text = ("obl-sim: FINDING @0 negative-counter world.guests=-1\n"
                "obl-sim: soft samples=2 findings=1 file=testlogs/chaos/a-findings.tsv\n"
                "obl-sim: end ticks=10 commands=0 applied=0 rejected=0 rejected-checked=0 rejected-changed=0 "
                "reload=none hash=0123456789abcdef replay=testlogs/chaos/a.replay hashes=2\n"
                "obl-sim: PASS\n")
        with patch("chaos_runs.run_host", return_value=(0, text, 0.1)):
            result = chaos_runs.check(Path("host"), Path("game"), Path("a.chaos"), 1, 30, replays=0)
        self.assertTrue(result.passed)
        self.assertEqual(result.failure, "")
        self.assertIn("soft samples=2 findings=1 file=", result.detail)

    def test_fatal_error_without_end_line_names_the_cause(self):
        text = "obl-sim: FAILED fatal-error fixture formatted argument 42\n"
        with patch("chaos_runs.run_host", return_value=(5, text, 0.1)):
            result = chaos_runs.check(Path("host"), Path("game"), Path("a.chaos"), 1, 30)
        self.assertFalse(result.passed)
        self.assertEqual(result.detail, text.strip()[len("obl-sim: "):])

    def test_replay_parts_keep_header_and_commands(self):
        header, commands = replay_parts(REPLAY)
        self.assertEqual(header, ["OBL-replay 1", "start scenario sandbox1", "seed 3", "fixed-dt 0.02", "hash-schema 2"])
        self.assertEqual(commands, [(0, "set-paused paused=1"), (10, "dozer x=1 y=2"), (20, "demolish object=7"),
                                    (30, "hire-staff profession=Cleaner")])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "c.replay"
            write_commands(header, commands[1:2], 40, path)
            self.assertEqual(path.read_text(encoding="utf-8").splitlines()[-2:], ["@10 dozer x=1 y=2", "end 40"])

    def test_variant_rewrites_seed_ticks_and_commands_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            run = tmp / "a.chaos"
            run.write_text("OBL-chaos 1\nstart scenario s1\nseed 1\nmax-ticks 3000\nsave-at 1500\ncommands list.replay\n", encoding="utf-8")
            self.assertEqual(variant(run, None, None, tmp), run)
            out = tmp / "out"
            out.mkdir()
            copy = variant(run, 4, 1000, out)
            keys = read_keys(copy.read_text(encoding="utf-8"))
            self.assertEqual(copy.name, "a-seed4.chaos")
            self.assertEqual((keys["seed"], keys["max-ticks"], keys["save-at"]), ("4", "1000", "500"))
            self.assertEqual(Path(keys["commands"]), (tmp / "list.replay").resolve())

    def test_signature_names_the_first_hard_failure(self):
        self.assertEqual(signature(["FAILED @12 rejected command changed the world: dozer x=1 y=2 (sections=map)"]), "rejected-changed")
        self.assertEqual(signature(["FAILED reload @1500 hash=0 expected=1 sections=world"]), "reload")
        self.assertEqual(signature(["FAILED tick 9 left gameplay (in-world=0 tick=9)"]), "left-gameplay")
        self.assertEqual(signature([]), "failed")

    def test_shrink_keeps_only_the_commands_the_failure_needs(self):
        # A fake host: the run fails ("reload") when it plays the demolish and the hire and lasts at least 60 ticks.
        def fake_check(host, game_dir, run_file, seed, timeout, replays=1):
            keys = read_keys(run_file.read_text(encoding="utf-8"))
            _, commands = replay_parts((run_file.parent / keys["commands"]).read_text(encoding="utf-8"))
            texts = [c for _, c in commands]
            failing = "demolish object=7" in texts and "hire-staff profession=Cleaner" in texts and int(keys["max-ticks"]) >= 60
            return Result(run_file.name, seed, not failing, "reload" if failing else "", 0.0, "")

        original = chaos_runs.check
        chaos_runs.check = fake_check
        try:
            with tempfile.TemporaryDirectory() as tmp:
                tmp = Path(tmp)
                run = tmp / "s.chaos"
                run.write_text("OBL-chaos 1\nstart scenario sandbox1\nseed 3\nmax-ticks 100\nsave-at 40\nmode tricky\n", encoding="utf-8")
                replay = tmp / "s.replay"
                replay.write_text(REPLAY, encoding="utf-8")
                failure = Result("s.chaos", 3, False, "reload", 0.0, "", replay)
                shrunk = chaos_runs.shrink(Path("host"), tmp, run, failure, 10, tmp / "shrunk")
                self.assertIsNotNone(shrunk)
                keys = read_keys(shrunk.read_text(encoding="utf-8"))
                self.assertNotIn("mode", keys)
                self.assertEqual((keys["max-ticks"], keys["save-at"], keys["seed"]), ("60", "40", "3"))
                _, commands = replay_parts((shrunk.parent / keys["commands"]).read_text(encoding="utf-8"))
                self.assertEqual(commands, [(20, "demolish object=7"), (30, "hire-staff profession=Cleaner")])
                self.assertEqual(sorted(p.name for p in (tmp / "shrunk").iterdir()), ["s-reload.chaos", "s-reload.replay"])
        finally:
            chaos_runs.check = original

    def test_shrink_with_runs_keeps_an_intermittent_failure(self):
        # The run fails ("crash") only in every third attempt, and only while it plays the demolish.
        attempts = []

        def fake_check(host, game_dir, run_file, seed, timeout, replays=1):
            keys = read_keys(run_file.read_text(encoding="utf-8"))
            _, commands = replay_parts((run_file.parent / keys["commands"]).read_text(encoding="utf-8"))
            attempts.append(1)
            failing = "demolish object=7" in [c for _, c in commands] and len(attempts) % 3 == 0
            return Result(run_file.name, seed, not failing, "crash" if failing else "", 0.0, "")

        original = chaos_runs.check
        chaos_runs.check = fake_check
        try:
            with tempfile.TemporaryDirectory() as tmp:
                tmp = Path(tmp)
                run = tmp / "s.chaos"
                run.write_text("OBL-chaos 1\nstart scenario sandbox1\nseed 3\nmax-ticks 100\n", encoding="utf-8")
                replay = tmp / "s.replay"
                replay.write_text(REPLAY, encoding="utf-8")
                failure = Result("s.chaos", 3, False, "crash", 0.0, "", replay)
                self.assertIsNone(chaos_runs.shrink(Path("host"), tmp, run, failure, 10, tmp / "one", runs=1))
                attempts.clear()
                shrunk = chaos_runs.shrink(Path("host"), tmp, run, failure, 10, tmp / "three", runs=3)
                self.assertIsNotNone(shrunk)
                keys = read_keys(shrunk.read_text(encoding="utf-8"))
                self.assertNotIn("mode", keys)
                _, commands = replay_parts((shrunk.parent / keys["commands"]).read_text(encoding="utf-8"))
                self.assertEqual(commands, [(20, "demolish object=7")])
        finally:
            chaos_runs.check = original


if __name__ == "__main__":
    unittest.main()
