"""Bot runner evidence and tolerance bands (tools/bot_runs.py) from synthetic outcomes; no game data or headless host needed."""
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bot_runs import (BOTS, CATALOG, Outcome, Run, allowed, check_bands, dump_bands, evidence, load_bands, parse_output, read_run, run_one,
                      record_bands, run_one, scenario_id, with_seed)


def outcome(start, expect, passed, result, name="x.bot", seed=1):
    return Outcome(Run(Path(name), "bot", start, expect), passed, result, 1.0, "", seed)


HOST_OUTPUT = """obl-sim: bot b start s1 seed=2 fixed-dt=0.02 max-ticks=900 expect=won
obl-sim: sample tick=0 day=0 hour=9 cash=25000 guests=94 rating=45
obl-sim: @0 set-game-speed level=2
obl-sim: sample tick=313 day=0 hour=10 cash=12746 guests=94 rating=47
obl-sim: end ticks=400 outcome=won day=0 hour=10.30 cash=12800 guests=95 rating=48 commands=1 applied=1 rejected=0 hash=e00949a0094e8ed0 ms=10 ticks-per-s=40000
obl-sim: PASS
"""


def played(seed, ticks, cash, samples):
    o = outcome("s1", "won", True, "won", "build_tree_s1.bot", seed)
    o.end = {"ticks": ticks, "outcome": "won", "day": 0, "hour": 10.0, "cash": cash, "guests": 100, "rating": 50, "hash": "h"}
    o.samples = samples
    return o


class BotRunsTests(unittest.TestCase):
    def test_fatal_error_without_end_line_names_the_cause(self):
        text = "obl-sim: FAILED fatal-error fixture formatted argument 42\n"
        with patch("bot_runs.subprocess.run") as process:
            process.return_value.returncode = 5
            process.return_value.stdout = text.encode()
            result = run_one(Path("host"), Path("game"), Run(Path("a.bot"), "bot", "s1", "won"), 1, Path("tmp"), 30)
        self.assertFalse(result.passed)
        self.assertEqual(result.detail, text.strip())

    def test_reads_the_runner_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "a.bot"
            path.write_text("# c\nOBL-bot 1\nbot build-tree-s1\nstart scenario s1\nseed 4\nmax-ticks 9\nexpect lost\n", encoding="utf-8")
            run = read_run(path)
            self.assertEqual((run.bot, run.start, run.expect, run.seed), ("build-tree-s1", "s1", "lost", 4))
            self.assertEqual(run.bands_path.name, "a.bands.json")
            path.write_text("OBL-bot 1\nbot wait\nstart scenario 3\nmax-ticks 9\n", encoding="utf-8")
            self.assertEqual((read_run(path).expect, read_run(path).seed), ("won", 1))

    def test_another_seed_replaces_or_adds_the_seed_line(self):
        self.assertEqual(with_seed("OBL-bot 1\nbot b\nseed 1\nmax-ticks 9\n", 3), "OBL-bot 1\nbot b\nseed 3\nmax-ticks 9\n")
        self.assertEqual(with_seed("# c\nOBL-bot 1\nbot b\n", 2), "# c\nOBL-bot 1\nseed 2\nbot b\n")

    def test_another_seed_keeps_the_plan_next_to_the_run_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "a.bot"
            path.write_text("OBL-bot 1\nbot wait\nstart scenario 3\nseed 1\nmax-ticks 9\n", encoding="utf-8")
            path.with_suffix(".plan").write_text("homes: hut\n", encoding="utf-8")
            scratch = root / "scratch"
            scratch.mkdir()
            with patch("bot_runs.subprocess.run", side_effect=FileNotFoundError):
                try:
                    run_one(Path("host"), root, read_run(path), 2, scratch, 1)
                except FileNotFoundError:
                    pass
            self.assertEqual((scratch / "a-seed2.plan").read_text(encoding="utf-8"), "homes: hut\n")

    def test_parses_the_end_line_and_the_hourly_samples(self):
        end, samples = parse_output(HOST_OUTPUT)
        self.assertEqual(end, {"ticks": 400, "outcome": "won", "day": 0, "hour": 10.3, "cash": 12800.0, "guests": 95, "rating": 48,
                               "hash": "e00949a0094e8ed0"})
        self.assertEqual(samples, [[0, 0, 9, 25000.0, 94, 45], [313, 0, 10, 12746.0, 94, 47]])
        self.assertEqual(parse_output("obl-sim: FAILED x\n")[0], {})

    def test_scenario_ids(self):
        self.assertEqual(scenario_id("s1"), "s1")
        self.assertEqual(scenario_id("12"), "s12")
        self.assertIsNone(scenario_id("sandbox2"))

    def test_bands_span_the_seeds_and_keep_the_hours_every_seed_reached(self):
        a = played(1, 1000, 2000, [[0, 0, 9, 25000, 90, 40], [300, 0, 10, 12000, 94, 47], [600, 0, 11, 5000, 95, 50]])
        b = played(2, 1200, 2600, [[0, 0, 9, 25000, 94, 45], [300, 0, 10, 13000, 96, 49]])
        bands = record_bands(a.run, [a, b])
        self.assertEqual(bands["seeds"], [1, 2])
        self.assertEqual(bands["ticks"], [1000, 1200])
        self.assertEqual(bands["end"]["cash"], [2000, 2600])
        self.assertEqual(bands["hours"], [{"day": 0, "hour": 9, "cash": [25000, 25000], "guests": [90, 94], "rating": [40, 45]},
                                          {"day": 0, "hour": 10, "cash": [12000, 13000], "guests": [94, 96], "rating": [47, 49]}])
        self.assertEqual(json.loads(dump_bands(bands)), bands)

    def test_tolerance_is_the_widest_of_floor_relative_and_half_spread(self):
        self.assertEqual(allowed("rating", [50, 52]), (42, 60))  # floor 8
        self.assertEqual(allowed("cash", [10000, 10000]), (8500, 11500))  # 15 %
        self.assertEqual(allowed("ticks", [1000, 3000]), (0, 4000))  # half the spread

    def test_a_drift_or_another_outcome_fails_the_bands(self):
        a = played(1, 1000, 2000, [[0, 0, 9, 25000, 94, 45], [300, 0, 10, 12000, 94, 47]])
        bands = record_bands(a.run, [a])
        self.assertEqual(check_bands(bands, a.end, a.samples), [])
        # An earlier end skips the hours it never reached; the ticks band judges it.
        self.assertEqual(check_bands(bands, dict(a.end, ticks=900), a.samples[:1]), [])
        doubled = check_bands(bands, dict(a.end, cash=5000), [a.samples[0], [300, 0, 10, 24000, 94, 47]])
        self.assertEqual(doubled, ["cash at the end 5000 outside 1500..2500 (seeds 2000..2000)",
                                   "cash at day 0 10:00 24000 outside 10200..13800 (seeds 12000..12000)"])
        self.assertIn("ticks 2000 outside 700..1300 (seeds 1000..1000)", check_bands(bands, dict(a.end, ticks=2000), a.samples))
        self.assertTrue(check_bands(bands, dict(a.end, outcome="lost"), a.samples)[0].startswith("outcome lost"))

    def test_only_a_run_file_won_on_every_seed_completes(self):
        catalog = ["s1", "s2", "s3", "s4"]
        report = evidence([outcome("s1", "won", False, "lost"), outcome("s1", "won", True, "won", "y.bot"),
                           outcome("s2", "lost", True, "lost", "z.bot"), outcome("s3", "won", False, "running", "w.bot"),
                           outcome("s4", "won", True, "won", "v.bot", 1), outcome("s4", "won", False, "won", "v.bot", 2),
                           outcome("sandbox1", "won", True, "won", "u.bot")], catalog, "abc")
        self.assertEqual(report["results"], [{"scenario": "s1", "status": "pass", "completed": True},
                                             {"scenario": "s3", "status": "fail", "completed": False},
                                             {"scenario": "s4", "status": "fail", "completed": False}])
        self.assertEqual([(r["file"], r["seed"], r["passed"]) for r in report["runs"]][-3:],
                         [("v.bot", 1, True), ("v.bot", 2, False), ("u.bot", 1, True)])

    def test_catalog_lists_the_campaign(self):
        catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
        self.assertEqual(catalog, [f"s{n}" for n in range(1, 13)])

    def test_every_run_file_has_valid_bands(self):
        for path in sorted(BOTS.glob("*.bot")):
            run = read_run(path)
            bands = load_bands(run)
            self.assertIsNotNone(bands, f"{path.name} has no bands (tools/bot_runs.py --record)")
            self.assertEqual((bands["run"], bands["bot"], bands["outcome"]), (path.name, run.bot, run.expect))
            self.assertLessEqual(bands["ticks"][0], bands["ticks"][1])


if __name__ == "__main__":
    unittest.main()
