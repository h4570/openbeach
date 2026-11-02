#!/usr/bin/env python3
"""Run the scenario bots headless, check their metrics against tolerance bands and write the evidence (docs/architecture/bots/index.md).

Runs every tests/bots/*.bot (or those matching --filter) through the headless host: OpenBeach.exe on Windows,
obl-sim on Linux, once per seed. Each run must reach the run file's outcome, and its metrics (ticks to the outcome, the
end values, cash/guests/rating at every game hour) must stay inside the bands of tests/bots/<run>.bands.json, so a win
through a bug or a behavior drift fails even when the bot still wins. Prints one line per run and writes the evidence file
(docs/architecture/bots/metrics.md). Exit code 0 when every run passed, 1 otherwise,
2 bad arguments, 77 when the host has no prepared game directory (ctest SKIP).

    python tools/bot_runs.py [--game-dir run] [--host PATH] [--out run/tmp/bots.json] [--filter PATTERN ...]
                             [--seeds N ... | --run-seed | --first-seed] [--record] [--jobs N]

Without --seeds a run file plays the seeds its bands were recorded with; --run-seed plays only its own `seed` (ctest).
--record plays --seeds (default 1 2 3) and rewrites the bands files from what they reached. The evidence names the
commit it ran on; a working tree with changes writes `<commit>-dirty`, which is never credited.
--jobs N (default: the CPU count on Linux, 1 on Windows) plays N runs at once, each in a worker game directory next to
--out (tools/sim_workers.py); the lines print as runs finish, the evidence keeps run file and seed order.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import fnmatch
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

import sim_workers

REPO = Path(__file__).resolve().parent.parent
BOTS = REPO / "tests" / "bots"
# The own L0 pack's runs (docs/build/ci.md "L0 tests without game data"): they play with OBL_PACK=l0 and are not campaign scenarios.
L0_BOTS = BOTS / "l0"
CATALOG = BOTS / "scenarios.json"


def set_test_root(root: Path) -> None:
    """Take the run files from `root`, a directory laid out like tests/."""
    global BOTS, L0_BOTS, CATALOG
    BOTS, L0_BOTS, CATALOG = root / "bots", root / "bots" / "l0", root / "bots" / "scenarios.json"


END_LINE = re.compile(r"obl-sim: end ticks=(\d+) outcome=(\w+) day=(-?\d+) hour=([\d.]+) cash=(-?[\d.]+) guests=(-?\d+) "
                      r"rating=(-?\d+) [^\n]*?hash=(\w+)[^\n]*")
SAMPLE_LINE = re.compile(r"obl-sim: sample tick=(\d+) day=(-?\d+) hour=(\d+) cash=(-?[\d.]+) guests=(-?\d+) rating=(-?\d+)")
EXIT_SKIP = 77  # the host's ctest SKIP: no prepared game directory
DEFAULT_RECORD_SEEDS = [1, 2, 3]
BANDS_SCHEMA = 1

# The tolerance around the range the recorded seeds reached: a value passes inside [min - pad, max + pad] with
# pad = max(floor, rel * max(|min|, |max|), (max - min) / 2). Wide enough for refactors that shift a purchase by an hour,
# narrow enough that a doubled income, an emptied beach or a win a day early fails. Policy lives here, the bands files hold
# only what the seeds reached, so the policy can change without re-recording.
TOLERANCE = {"ticks": (0.10, 300), "cash": (0.15, 500), "guests": (0.10, 10), "rating": (0.0, 8)}
METRICS = ("cash", "guests", "rating")


@dataclass
class Run:
    path: Path
    bot: str
    start: str
    expect: str
    seed: int = 1

    @property
    def bands_path(self) -> Path:
        return self.path.with_name(self.path.stem + ".bands.json")

    @property
    def is_l0(self) -> bool:
        return self.path.parent == L0_BOTS


@dataclass
class Outcome:
    run: Run
    passed: bool
    outcome: str
    seconds: float
    detail: str
    seed: int = 1
    end: dict = field(default_factory=dict)  # ticks, day, hour, cash, guests, rating, hash
    samples: list = field(default_factory=list)  # [tick, day, hour, cash, guests, rating] at every new game hour
    band: str = "none"  # pass / fail / none (no bands file)
    violations: list = field(default_factory=list)
    skipped: bool = False


def read_run(path: Path) -> Run:
    """The keys the runner needs from a bot run file (the host validates the whole file)."""
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            key, _, rest = line.partition(" ")
            values[key] = rest.strip()
    start = values.get("start", "")
    return Run(path, values.get("bot", ""), start.split(" ", 1)[1].strip() if start.startswith("scenario ") else "",
               values.get("expect", "won"), int(values.get("seed", "1")))


def with_seed(text: str, seed: int) -> str:
    """The run file's text playing another seed: its `seed` line replaced, or one added after the header."""
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if line.strip().partition(" ")[0] == "seed":
            lines[i] = f"seed {seed}"
            return "\n".join(lines) + "\n"
    header = next(i for i, line in enumerate(lines) if line.strip().startswith("OBL-bot"))
    return "\n".join(lines[:header + 1] + [f"seed {seed}"] + lines[header + 1:]) + "\n"


def scenario_id(start: str) -> str | None:
    """The catalog id of a campaign start (`s<n>` or `<n>`, as command::ParseStartScenario); None for a sandbox island."""
    m = re.fullmatch(r"s?(\d+)", start)
    return f"s{int(m.group(1))}" if m else None


def parse_output(out: str) -> tuple[dict, list]:
    """The end values and the hourly samples of one host run; an empty dict when it printed no end line."""
    samples = [[int(m[1]), int(m[2]), int(m[3]), float(m[4]), int(m[5]), int(m[6])] for m in SAMPLE_LINE.finditer(out)]
    m = END_LINE.search(out)
    if not m:
        return {}, samples
    return {"ticks": int(m[1]), "outcome": m[2], "day": int(m[3]), "hour": float(m[4]), "cash": float(m[5]),
            "guests": int(m[6]), "rating": int(m[7]), "hash": m[8]}, samples


# --- tolerance bands -----------------------------------------------------------------------------------------------------

def span(values: list[float]) -> list[float]:
    return [min(values), max(values)]


def record_bands(run: Run, outcomes: list[Outcome]) -> dict:
    """The bands of one run file from its seeds: every seed reached the expected outcome (checked by the caller). The hourly
    curve keeps the hours every seed reached, the cash, guests and rating at the first tick of each."""
    hours = None
    for o in outcomes:
        reached = {(s[1], s[2]): s for s in o.samples}
        hours = reached.keys() if hours is None else hours & reached.keys()
    curve = []
    for day, hour in sorted(hours or []):
        points = [next(s for s in o.samples if (s[1], s[2]) == (day, hour)) for o in outcomes]
        curve.append({"day": day, "hour": hour, **{name: span([p[3 + i] for p in points]) for i, name in enumerate(METRICS)}})
    return {"schema_version": BANDS_SCHEMA, "run": run.path.name, "bot": run.bot, "start": run.start, "outcome": run.expect,
            "seeds": [o.seed for o in outcomes], "ticks": span([o.end["ticks"] for o in outcomes]),
            "end": {name: span([o.end[name] for o in outcomes]) for name in METRICS}, "hours": curve}


def dump_bands(bands: dict) -> str:
    """A bands file: one line per key and per hour, so a re-recording diffs hour by hour."""
    keys = [f' {json.dumps(k)}: {json.dumps(v)}' for k, v in bands.items() if k != "hours"]
    hours = [f"  {json.dumps(point)}" for point in bands["hours"]]
    return "{\n" + ",\n".join(keys + [' "hours": [\n' + ",\n".join(hours) + "\n ]"]) + "\n}\n"


def allowed(metric: str, recorded: list[float]) -> tuple[float, float]:
    rel, floor = TOLERANCE[metric]
    low, high = recorded
    pad = max(floor, rel * max(abs(low), abs(high)), (high - low) / 2)
    return low - pad, high + pad


def check_bands(bands: dict, end: dict, samples: list) -> list[str]:
    """What lies outside the bands, one line per value (empty when the run is inside them)."""
    problems = []

    def check(metric: str, where: str, value: float, recorded: list[float]) -> None:
        low, high = allowed(metric, recorded)
        if not low <= value <= high:
            problems.append(f"{metric} {where}{value:g} outside {low:g}..{high:g} (seeds {recorded[0]:g}..{recorded[1]:g})")

    if end.get("outcome") != bands["outcome"]:
        problems.append(f"outcome {end.get('outcome')} but the bands were recorded for {bands['outcome']}")
    check("ticks", "", end["ticks"], bands["ticks"])
    for name in METRICS:
        check(name, "at the end ", end[name], bands["end"][name])
    reached = {(s[1], s[2]): s for s in samples}
    for point in bands["hours"]:
        sample = reached.get((point["day"], point["hour"]))
        if sample is None:
            continue  # the run ended before this hour: the ticks band judges how much earlier
        for i, name in enumerate(METRICS):
            check(name, f"at day {point['day']} {point['hour']:02d}:00 ", sample[3 + i], point[name])
    return problems


def load_bands(run: Run) -> dict | None:
    if not run.bands_path.exists():
        return None
    bands = json.loads(run.bands_path.read_text(encoding="utf-8"))
    if bands.get("schema_version") != BANDS_SCHEMA or not bands.get("seeds"):
        raise ValueError(f"{run.bands_path.name}: bands need schema_version {BANDS_SCHEMA} and seeds")
    return bands


# --- evidence ------------------------------------------------------------------------------------------------------------

def evidence(outcomes: list[Outcome], catalog: list[str], revision: str) -> dict:
    """One result per catalog scenario that has a winning run file: pass/completed when one run file won it on every seed,
    inside its bands. `runs` adds every run's metrics for later balance reports (schema 1 consumers ignore it)."""
    by_file: dict[Path, list[Outcome]] = {}
    for o in outcomes:
        by_file.setdefault(o.run.path, []).append(o)
    results = {}
    for path, runs in by_file.items():
        run = runs[0].run
        scenario = scenario_id(run.start)
        if run.is_l0 or scenario not in catalog or run.expect != "won":
            continue
        won = all(o.passed and o.outcome == "won" for o in runs)
        if won or scenario not in results:
            results[scenario] = {"scenario": scenario, "status": "pass" if won else "fail", "completed": won}
    return {"schema_version": 1, "revision": revision, "results": [results[s] for s in catalog if s in results],
            "runs": [{"file": o.run.path.name, "bot": o.run.bot, "start": o.run.start, "expect": o.run.expect, "seed": o.seed,
                      "passed": o.passed, "outcome": o.outcome, "band": o.band, "violations": o.violations,
                      "seconds": round(o.seconds, 1), "end": o.end, "samples": o.samples} for o in outcomes]}


def revision() -> str:
    try:
        head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip()
        dirty = subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=normal"], cwd=REPO, text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"  # no usable git (a Windows worktree under WSL): never credited
    return head + "-dirty" if dirty else head


def default_host() -> Path | None:
    if sys.platform == "win32":
        # The newer of the two configurations build.ps1 produces.
        hosts = [REPO / "build" / a / "bin" / c / "OpenBeach.exe" for a in ("x64",) for c in ("RelWithDebInfo", "Debug")]
        hosts = [h for h in hosts if h.exists()]
        return max(hosts, key=lambda h: h.stat().st_mtime) if hosts else None
    sim = Path(os.environ.get("OBL_SIM_BUILD", REPO / "build" / "linux-sim-x64")) / "bin" / "obl-sim"
    return sim if sim.exists() else None


def run_one(host: Path, game_dir: Path, run: Run, seed: int, scratch: Path, timeout: int) -> Outcome:
    path = run.path
    if seed != run.seed:
        path = scratch / f"{run.path.stem}-seed{seed}.bot"
        path.write_text(with_seed(run.path.read_text(encoding="utf-8"), seed), encoding="utf-8")
        # The host finds the bot's tables as `<run file stem>.plan` next to the run file; without it the bot plays an empty plan.
        plan = run.path.with_suffix(".plan")
        if plan.exists():
            shutil.copyfile(plan, path.with_suffix(".plan"))
    start = time.monotonic()
    try:
        # The L0 runs select their own pack; the base pack stays installed for the others.
        env = dict(os.environ, OBL_PACK="l0") if run.is_l0 else None
        proc = subprocess.run([str(host), "--game-dir", str(game_dir), "--headless", str(path)], stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=timeout, env=env)
        code, out = proc.returncode, proc.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        return Outcome(run, False, "running", time.monotonic() - start, "timed out", seed)
    seconds = time.monotonic() - start
    if code == EXIT_SKIP:
        return Outcome(run, False, "running", seconds, "skipped: no prepared game directory", seed, skipped=True)
    end, samples = parse_output(out)
    if not end:
        silent = "" if out.strip() else " and no output (another game running in this game directory?)"
        failed = next((l for l in out.splitlines() if "FAILED" in l), f"no end line, exit code {code}{silent}")
        return Outcome(run, False, "running", seconds, failed.strip()[:160], seed, samples=samples)
    passed = code == 0 and "obl-sim: PASS" in out
    detail = END_LINE.search(out).group(0)[len("obl-sim: "):]
    return Outcome(run, passed, end["outcome"], seconds, detail, seed, end, samples)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game-dir", default=str(REPO / "run"), help="game directory with the imported pack")
    parser.add_argument("--host", help="OpenBeach.exe or obl-sim (default: the newest build of this checkout)")
    parser.add_argument("--test-root", help="directory laid out like tests/ to take the run files from")
    parser.add_argument("--out", default=str(REPO / "run" / "tmp" / "bots.json"), help="evidence file to write")
    parser.add_argument("--filter", nargs="*", default=[], help="fnmatch patterns on run file names (build_tree_s1, *_s1)")
    parser.add_argument("--seeds", nargs="*", type=int, help="seeds to play (default: the bands' seeds; 1 2 3 with --record)")
    parser.add_argument("--run-seed", action="store_true", help="play only the run file's own seed, still inside the bands (ctest)")
    parser.add_argument("--first-seed", action="store_true", help="play only the first seed of each run file's bands (CI push to main)")
    parser.add_argument("--record", action="store_true", help="rewrite tests/bots/<run>.bands.json from these runs")
    parser.add_argument("--timeout", type=int, default=900, help="seconds per run")
    parser.add_argument("--jobs", type=int, default=sim_workers.default_jobs(), help="runs at once (default: CPU count, 1 on Windows)")
    args = parser.parse_args()
    if args.test_root:
        set_test_root(Path(args.test_root).resolve())
    host = Path(args.host) if args.host else default_host()
    if not host or not host.exists():
        print("no headless host: build first (build.ps1, tools/obl_sim_linux.sh) or pass --host", file=sys.stderr)
        return 2
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    runs = [read_run(p) for p in sorted([*BOTS.glob("*.bot"), *L0_BOTS.glob("*.bot")])
            if not args.filter or any(fnmatch.fnmatch(p.stem, f) or fnmatch.fnmatch(p.name, f) for f in args.filter)]
    if not runs:
        print(f"no run file matches {args.filter}", file=sys.stderr)
        return 2
    out = Path(args.out)
    scratch = out.parent / "bot-seeds"
    scratch.mkdir(parents=True, exist_ok=True)
    commit = revision()
    game_dir = Path(args.game_dir).resolve()
    plan = []  # (run, bands, seeds) per run file
    for run in runs:
        try:
            bands = None if args.record else load_bands(run)
        except (ValueError, json.JSONDecodeError) as e:
            print(f"FAIL {run.path.name}: {e}", file=sys.stderr)
            return 1
        seeds = [run.seed] if args.run_seed else args.seeds or (DEFAULT_RECORD_SEEDS if args.record else bands["seeds"] if bands else [run.seed])
        plan.append((run, bands, seeds[:1] if args.first_seed else seeds))
    tasks = [(run, bands, seed) for run, bands, seeds in plan for seed in seeds]

    def judge(run: Run, bands: dict | None, o: Outcome) -> Outcome:
        if o.end and not args.record:
            if bands is None:
                o.band, o.violations = "fail", [f"no bands: record them with python tools/bot_runs.py --record --filter {run.path.stem}"]
            else:
                o.violations = check_bands(bands, o.end, o.samples)
                o.band = "fail" if o.violations else "pass"
            o.passed = o.passed and o.band == "pass"
        return o

    def show(o: Outcome) -> None:
        run = o.run
        print(f"{'PASS' if o.passed else 'FAIL'} {run.path.name:<24} seed={o.seed} {run.bot} {run.start} expect={run.expect} "
              f"outcome={o.outcome} bands={o.band} {o.seconds:.1f}s  {o.detail}", flush=True)
        for problem in o.violations[:8]:
            print(f"     {problem}")
        if len(o.violations) > 8:
            print(f"     ... {len(o.violations) - 8} more")

    results: list[Outcome | None] = [None] * len(tasks)
    workers = max(1, min(args.jobs, len(tasks)))
    if workers == 1:
        for i, (run, bands, seed) in enumerate(tasks):
            o = run_one(host, game_dir, run, seed, scratch, args.timeout)
            if o.skipped:
                print(f"SKIP {run.path.name} seed={seed}: {o.detail}")
                return EXIT_SKIP
            results[i] = judge(run, bands, o)
            show(results[i])
    else:
        dirs = out.parent / "bot-workers"
        pool: queue.Queue = queue.Queue()
        for w in range(workers):
            pool.put(sim_workers.worker_game_dir(game_dir, dirs / f"w{w}"))

        def play(i: int) -> int:
            run, bands, seed = tasks[i]
            where = pool.get()
            try:
                results[i] = judge(run, bands, run_one(host, where, run, seed, scratch, args.timeout))
            finally:
                pool.put(where)
            return i

        with concurrent.futures.ThreadPoolExecutor(workers) as executor:
            for future in concurrent.futures.as_completed([executor.submit(play, i) for i in range(len(tasks))]):
                o = results[future.result()]
                if o.skipped:
                    print(f"SKIP {o.run.path.name} seed={o.seed}: {o.detail}")
                    return EXIT_SKIP
                show(o)
        shutil.rmtree(dirs, ignore_errors=True)
    outcomes = [o for o in results if o is not None]
    failed_record = False
    if args.record:
        for run, _, seeds in plan:
            mine = [o for o in outcomes if o.run is run]
            if all(o.passed for o in mine):
                run.bands_path.write_text(dump_bands(record_bands(run, mine)), encoding="utf-8")
                print(f"     recorded {run.bands_path.relative_to(REPO)} from seeds {seeds}")
            else:
                failed_record = True
                print(f"     not recorded: {run.path.name}: a seed missed the expected outcome {run.expect}")
    report = evidence(outcomes, catalog, commit)
    out.write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    completed = sum(r["completed"] for r in report["results"])
    print(f"{sum(o.passed for o in outcomes)}/{len(outcomes)} runs passed; scenarios completed {completed}/{len(catalog)}; "
          f"evidence {out} ({commit})")
    return 0 if all(o.passed for o in outcomes) and not failed_record else 1


if __name__ == "__main__":
    sys.exit(main())
