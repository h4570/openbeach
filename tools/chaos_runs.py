#!/usr/bin/env python3
"""Run the chaos bot headless, check its hard invariants and shrink failing seeds (docs/testing/chaos-bot.md).

Runs every tests/chaos/*.chaos (or those matching --filter) through the headless host: OpenBeach.exe on Windows,
obl-sim on Linux. The host checks that the run reaches max-ticks, that rejected commands leave the world hash unchanged and
that a save and reload at `save-at` continues to the same hashes; it writes the run as a replay, which this runner then plays
in a fresh process (determinism: the same seed gives the same hashes). Prints one line per run. Exit code 0 when every run
passed, 1 otherwise, 2 bad arguments, 77 when the host has no prepared game directory (ctest SKIP).

    python tools/chaos_runs.py [--game-dir run] [--host PATH] [--filter PATTERN ...] [--seeds N ...] [--ticks N]
                               [--shrink] [--runs N] [--timeout S]

--seeds plays other seeds of the same run files (a copy under run/tmp/chaos/seeds/), --ticks another length (save-at
moves to the middle). --shrink cuts a failing run to a minimal command list that still fails the same way (drop commands
while the failure persists, then shorten the ticks) and writes it under run/tmp/chaos/shrunk/ as a replay of the commands
plus a .chaos file that plays them (`commands`); a finding moves from there into tests/replays/ with its fix.
--runs N replays each run N times and prints the distinct end hashes, and judges each shrink trial by N attempts, so an
intermittent failure (one that shows in some processes only) survives the trials that pass by chance.
"""
from __future__ import annotations

import argparse
import fnmatch
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CHAOS = REPO / "tests" / "chaos"
EXIT_SKIP = 77
# Replays per shrink trial of a determinism failure: an intermittent mismatch (about every second process in the first finding)
# must not look like a pass and let ddmin drop the command that causes it.
DETERMINISM_REPLAYS = 5
END_LINE = re.compile(r"obl-sim: end ticks=(\d+) commands=(\d+) applied=(\d+) rejected=(\d+) rejected-checked=(\d+) "
                      r"rejected-changed=(\d+) reload=(\w+) hash=(\w+) replay=(\S+)")
REPLAY_END = re.compile(r"obl-sim: end ticks=(\d+) hash=(\w+) checkpoints=(\d+) mismatches=(\d+)")


@dataclass
class Result:
    name: str
    seed: int
    passed: bool
    failure: str  # "" when passed, else a signature: crash, timeout, left-gameplay, rejected-changed, reload, determinism, ...
    seconds: float
    detail: str
    replay: Path | None = None
    lines: list = field(default_factory=list)  # FAILED lines of the host


def default_host() -> Path | None:
    if sys.platform == "win32":
        hosts = [REPO / "build" / a / "bin" / c / "OpenBeach.exe" for a in ("x64",) for c in ("RelWithDebInfo", "Debug")]
        hosts = [h for h in hosts if h.exists()]
        return max(hosts, key=lambda h: h.stat().st_mtime) if hosts else None
    sim = Path(os.environ.get("OBL_SIM_BUILD", REPO / "build" / "linux-sim-x64")) / "bin" / "obl-sim"
    return sim if sim.exists() else None


def read_keys(text: str) -> dict:
    keys = {}
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            key, _, rest = line.partition(" ")
            keys[key] = rest.strip()
    return keys


def write_run(keys: dict, path: Path) -> None:
    """A chaos run file from its keys (the magic line first)."""
    lines = ["OBL-chaos 1"] + [f"{k} {v}" for k, v in keys.items() if k != "OBL-chaos"]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def variant(path: Path, seed: int | None, ticks: int | None, scratch: Path) -> Path:
    """The run file with another seed or length, as a copy under `scratch` (a `commands` path made absolute)."""
    keys = read_keys(path.read_text(encoding="utf-8"))
    if seed is None and ticks is None:
        return path
    if seed is not None:
        keys["seed"] = str(seed)
    if ticks is not None:
        keys["max-ticks"] = str(ticks)
        if "save-at" in keys:
            keys["save-at"] = str(ticks // 2)
    if "commands" in keys:
        keys["commands"] = str((path.parent / keys["commands"]).resolve())
    copy = scratch / f"{path.stem}-seed{keys.get('seed', '1')}.chaos"
    write_run(keys, copy)
    return copy


def run_host(host: Path, game_dir: Path, target: Path, timeout: int) -> tuple[int | None, str, float]:
    start = time.monotonic()
    try:
        proc = subprocess.run([str(host), "--game-dir", str(game_dir), "--headless", str(target)], stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=timeout)
        return proc.returncode, proc.stdout.decode("utf-8", "replace"), time.monotonic() - start
    except subprocess.TimeoutExpired:
        return None, "", time.monotonic() - start


def signature(lines: list[str]) -> str:
    """The kind of the first hard failure the host reported."""
    for line in lines:
        if "rejected command changed the world" in line:
            return "rejected-changed"
        if "FAILED reload" in line or "reloaded world" in line:
            return "reload"
        if "left gameplay" in line:
            return "left-gameplay"
        if "save at tick" in line:
            return "save"
    return "failed"


def check(host: Path, game_dir: Path, run_file: Path, seed: int, timeout: int, replays: int = 1,
          all_replays: bool = False) -> Result:
    """One chaos run, then its recorded replay `replays` times, each in a fresh process (0: no determinism check). A
    nondeterminism often shows only in some processes (heap contents, uninitialized fields), so the shrinker replays more."""
    name = run_file.name
    code, out, seconds = run_host(host, game_dir, run_file, timeout)
    if code == EXIT_SKIP:
        return Result(name, seed, False, "skip", seconds, "no prepared game directory")
    if code is None:
        return Result(name, seed, False, "timeout", seconds, f"timed out after {timeout} s")
    failed = [l.strip()[len("obl-sim: "):] for l in out.splitlines() if l.startswith("obl-sim: FAILED") and l.strip() != "obl-sim: FAILED"]
    m = END_LINE.search(out)
    if not m:
        silent = "" if out.strip() else " and no output (another game running in this game directory?)"
        detail = failed[0] if failed else f"no end line, exit code {code:#x}{silent}"
        return Result(name, seed, False, "failed" if failed else "crash", seconds, detail[:200], lines=failed)
    replay = game_dir / m[9]
    detail = (f"ticks={m[1]} commands={m[2]} applied={m[3]} rejected={m[4]} checked={m[5]} changed={m[6]} reload={m[7]} "
              f"hash={m[8]}")
    soft = next((line[len("obl-sim: "):] for line in out.splitlines() if line.startswith("obl-sim: soft ")), "")
    if soft:
        detail += f"; {soft}"
    if code != 0 or "obl-sim: PASS" not in out:
        return Result(name, seed, False, signature(failed), seconds, f"{detail}; {failed[0] if failed else ''}", replay, failed)
    # Determinism: the recorded run in a fresh process reaches every checkpoint hash. With `all_replays` every replay runs
    # and the detail counts the distinct end hashes (the run's own included): an intermittent mismatch shows its rate.
    hashes, failure = [m[8]], ""
    for attempt in range(replays):
        code, out, more = run_host(host, game_dir, replay, timeout)
        seconds += more
        end = REPLAY_END.search(out)
        hashes.append(end[2] if end else f"exit-{code}")
        if (code != 0 or not end) and not failure:
            first = next((l.strip() for l in out.splitlines() if "MISMATCH" in l or "FAILED" in l), f"exit code {code}")
            sections = next((l.strip()[len("obl-sim: "):] for l in out.splitlines() if " sections=" in l), "")
            failure = f"replay {attempt + 1}: {first[:120]} {sections}"
            if not all_replays:
                break
    distinct = f" distinct-end-hashes={len(set(hashes))}/{len(hashes)}" if all_replays and replays > 1 else ""
    if failure:
        return Result(name, seed, False, "determinism", seconds, f"{detail}{distinct}; {failure}", replay)
    return Result(name, seed, True, "", seconds, f"{detail} replay=match{distinct}" if replays else detail, replay)


# --- shrinking ----------------------------------------------------------------------------------------------------------

def replay_parts(text: str) -> tuple[list[str], list[tuple[int, str]]]:
    """The header lines and the (tick, command) body lines of a replay; checkpoints and `end` are dropped."""
    header, commands = [], []
    for line in text.splitlines():
        if line.startswith("@"):
            tick, _, rest = line[1:].partition(" ")
            if not rest.startswith("hash "):
                commands.append((int(tick), rest))
        elif line.strip() and not line.startswith("end ") and not line.startswith("#"):
            header.append(line)
    return header, commands


def write_commands(header: list[str], commands: list[tuple[int, str]], end: int, path: Path) -> None:
    body = [f"@{t} {c}" for t, c in commands]
    path.write_text("\n".join(header + body + [f"end {end}"]) + "\n", encoding="utf-8")


def shrink(host: Path, game_dir: Path, run_file: Path, failure: Result, timeout: int, out_dir: Path,
           runs: int = 1) -> Path | None:
    """ddmin over the failing run's commands, then the shortest tick count that still fails the same way. A trial fails
    when any of `runs` attempts fails that way (a determinism trial: one run and `runs` replays, at least
    DETERMINISM_REPLAYS), so an intermittent failure is not lost when one attempt passes by chance."""
    if failure.replay is None or not failure.replay.exists():
        print("     shrink: no recorded replay to cut")
        return None
    keys = read_keys(run_file.read_text(encoding="utf-8"))
    keys.pop("mode", None)  # shrinking plays recorded commands, never regenerates scripted probes
    header, commands = replay_parts(failure.replay.read_text(encoding="utf-8"))
    ticks = int(keys["max-ticks"])
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = f"{run_file.stem}-{failure.failure}"
    trial_list, trial_run = out_dir / f"{stem}-trial.replay", out_dir / f"{stem}-trial.chaos"
    if failure.failure == "determinism":
        # The first differing checkpoint bounds the commands that matter; trials need no rejection hashes (a plain replay differed).
        keys["checkpoint-every"] = "50"
        keys["check-rejected"] = "0"
        keys.pop("save-at", None)
        first = re.search(r"@(\d+) hash=\w+ fields=\d+ expected=\w+ MISMATCH", failure.detail)
        if first:
            ticks = int(first[1])
            commands = [(t, c) for t, c in commands if t < ticks]
    trials = 0

    def fails(candidate: list[tuple[int, str]], end: int) -> bool:
        nonlocal trials
        trials += 1
        write_commands(header, candidate, end, trial_list)
        trial = dict(keys, **{"max-ticks": str(end), "commands": trial_list.name})
        if "save-at" in trial and int(trial["save-at"]) >= end:
            return False
        write_run(trial, trial_run)
        seed = int(keys.get("seed", "1"))
        if failure.failure == "determinism":
            result = check(host, game_dir, trial_run, seed, timeout, max(runs, DETERMINISM_REPLAYS))
            return not result.passed and result.failure == failure.failure
        for _ in range(runs):
            result = check(host, game_dir, trial_run, seed, timeout, 0)
            if not result.passed and result.failure == failure.failure:
                return True
        return False

    if not fails(commands, ticks):
        hint = "" if runs > 1 else "; an intermittent failure needs --runs N"
        print(f"     shrink: the command list alone does not repeat '{failure.failure}' in {runs} run(s) "
              f"(the generator's timing matters?{hint})")
        return None
    # ddmin: remove chunks while the failure persists, halving the chunk size; single commands until no removal helps.
    chunk = max(1, len(commands) // 2)
    while commands:
        i, removed = 0, False
        while i < len(commands):
            candidate = commands[:i] + commands[i + chunk:]
            if fails(candidate, ticks):
                commands, removed = candidate, True
            else:
                i += chunk
        print(f"     shrink: chunk {chunk}: {len(commands)} commands left ({trials} trials)", flush=True)
        if chunk == 1 and not removed:
            break
        chunk = max(1, chunk // 2)
    # The shortest run: binary search on the end tick above the last command (and the save point).
    low = max([t + 1 for t, _ in commands] + [int(keys.get("save-at", "0")) + 1, 1])
    high = ticks
    while low < high:
        middle = (low + high) // 2
        if fails(commands, middle):
            high = middle
        else:
            low = middle + 1
    ticks = high
    final_list, final_run = out_dir / f"{stem}.replay", out_dir / f"{stem}.chaos"
    write_commands(header, commands, ticks, final_list)
    write_run(dict(keys, **{"max-ticks": str(ticks), "commands": final_list.name}), final_run)
    trial_list.unlink(missing_ok=True)
    trial_run.unlink(missing_ok=True)
    print(f"     shrunk to {len(commands)} commands, {ticks} ticks after {trials} trials: {final_run}")
    return final_run


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game-dir", default=str(REPO / "run"), help="game directory with the imported pack")
    parser.add_argument("--host", help="OpenBeach.exe or obl-sim (default: the newest build of this checkout)")
    parser.add_argument("--test-root", help="directory laid out like tests/ to take the run files from")
    parser.add_argument("--filter", nargs="*", default=[], help="fnmatch patterns on run file names (sandbox1, s1_*)")
    parser.add_argument("--seeds", nargs="*", type=int, help="seeds to play (default: each run file's own)")
    parser.add_argument("--ticks", type=int, help="max-ticks for every run (save-at moves to the middle)")
    parser.add_argument("--shrink", action="store_true", help="cut each failing run to a minimal command list")
    parser.add_argument("--runs", type=int, default=1,
                        help="replays per determinism check (reports the distinct end hashes) and attempts per shrink trial")
    parser.add_argument("--timeout", type=int, default=900, help="seconds per host run")
    args = parser.parse_args()
    host = Path(args.host) if args.host else default_host()
    if not host or not host.exists():
        print("no headless host: build first (build.ps1, tools/obl_sim_linux.sh) or pass --host", file=sys.stderr)
        return 2
    files = [p for p in sorted((Path(args.test_root).resolve() / "chaos" if args.test_root else CHAOS).glob("*.chaos"))
             if not args.filter or any(fnmatch.fnmatch(p.stem, f) or fnmatch.fnmatch(p.name, f) for f in args.filter)]
    if not files:
        print(f"no chaos run file matches {args.filter}", file=sys.stderr)
        return 2
    game_dir = Path(args.game_dir).resolve()
    scratch = (Path(args.test_root).resolve().parent if args.test_root else REPO) / "run" / "tmp" / "chaos"
    (scratch / "seeds").mkdir(parents=True, exist_ok=True)
    results = []
    for path in files:
        own = int(read_keys(path.read_text(encoding="utf-8")).get("seed", "1"))
        for seed in args.seeds or [own]:
            target = variant(path, seed if seed != own else None, args.ticks, scratch / "seeds")
            r = check(host, game_dir, target, seed, args.timeout, max(args.runs, 1), all_replays=args.runs > 1)
            if r.failure == "skip":
                print(f"SKIP {path.name} seed={seed}: {r.detail}")
                return EXIT_SKIP
            results.append(r)
            verdict = "PASS" if r.passed else f"FAIL[{r.failure}]"
            print(f"{verdict} {path.name:<20} seed={seed} {r.seconds:.1f}s  {r.detail}", flush=True)
            for line in r.lines[1:6]:
                print(f"     {line[:200]}")
            if not r.passed and args.shrink:
                shrink(host, game_dir, target, r, args.timeout, scratch / "shrunk", args.runs)
    passed = sum(r.passed for r in results)
    print(f"{passed}/{len(results)} chaos runs passed")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
