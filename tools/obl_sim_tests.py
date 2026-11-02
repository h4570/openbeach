#!/usr/bin/env python3
"""Linux test runner for obl-sim (docs/architecture/linux-host.md, "Test runner").

Builds obl-sim (tools/obl_sim_linux.sh) and runs, against a prepared game directory, the headless checks the Windows
host runs through ctest and runtests.ps1: the headless replays and scripts of tests/unit/CMakeLists.txt, the reference
replays (tests/replays), the scenario bot runs (tests/bots), the golden hash fixtures of the default run and the @headless
scripts of tests/manifest.txt.
--windowed instead builds the windowed OpenBeach (-DOBL_LINUX_WINDOWED=ON) and runs the windowed game scripts of
tests/manifest.txt on it, each in its own hidden process (SDL offscreen video, dummy audio; docs/testing/linux-windowed.md).
Prints one PASS/FAIL line per entry and a table; exit code 0 when everything passed, 1 otherwise, 2 bad arguments.

    python3 tools/obl_sim_tests.py [--game-dir run] [--no-build | --sim HOST] [--filter PATTERN ...] [--solo] [--list]
                                   [--jobs N] [--windowed] [--test-root DIR]

Logs: <game dir>/testlogs/linux/<entry>.log (--windowed: testlogs/linux-windowed/) (the game log of the run, or the script's section of a batch).
--jobs N (default: the CPU count on Linux, 1 on Windows) runs N processes at once, each in a worker game directory
<game dir>/testlogs/linux/workers/w<i> (tools/sim_workers.py); results print as they finish, the table keeps entry order.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import fnmatch
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
TESTS = REPO / "tests"
UNIT = TESTS / "unit"
REPLAYS = TESTS / "replays"
ORACLE_ROOT = REPO


def set_test_root(root: Path) -> None:
    """Take the tests from `root`, a directory laid out like tests/; its parent stands for the repository in @check oracles."""
    global TESTS, UNIT, REPLAYS, ORACLE_ROOT
    TESTS, UNIT, REPLAYS, ORACLE_ROOT = root, root / "unit", root / "replays", root.parent


# Headers that need a process of their own (as Test-BatchableEntry in tools/testrun.ps1).
SOLO_HEADER = re.compile(r"^\s*#\s*@(env|gameini|expect-exit|check|expect-log|audio|own-process)\b")
TIMEOUT_HEADER = re.compile(r"^\s*#\s*@timeout\s+(\d+)")
GAMEINI_HEADER = re.compile(r"^\s*#\s*@gameini\s+([^.\s]+)\.([^=\s]+)=(\S*)")
ENV_HEADER = re.compile(r"^\s*#\s*@env\s+(\w+)=(\S*)")
# The frame cap of a windowed script (autotest.ps1 passes it as OBL_MAXFPS; headless runs are uncapped regardless).
MAXFPS_HEADER = re.compile(r"^\s*#\s*@maxfps\s+(\d+)")
CHECK_HEADER = re.compile(r"^\s*#\s*@check\s+(\S+)\s*(.*)$")
LINUX_WINDOWED_SKIP = re.compile(r"^\s*#\s*@linux-windowed-skip\b")
# Values every test starts from (autotest.ps1 $testBaselineIni).
# Language 5 (Polish, the supported build's): an L0 test's Language=0 would otherwise stay for every later test.
BASELINE_INI = {("Video", "ResWidth"): "800", ("Video", "ResHeight"): "600", ("Language", "Language"): "5"}
SCRIPT_FAILED = re.compile(r"autotest:.*\bFAILED\b|autotest: aborted|FatalError:")
GOLDEN_LINE = re.compile(
    r"autotest: world-hash schema=3 checkpoint=(\d+) steps=(\d+) fields=(\d+) hash=([0-9a-f]{16}) file=checkpoint-(\d+)\.tsv$")


@dataclass
class Result:
    name: str
    ok: bool
    seconds: float
    detail: str = ""


@dataclass
class Entry:
    """One run of obl-sim: `target` is a replay, script or script list; `check` judges (exit code, output, log)."""
    name: str
    target: Path
    check: object
    timeout: int = 600
    gameini: dict = field(default_factory=dict)
    env: dict = field(default_factory=dict)
    oracles: list = field(default_factory=list)
    hide: str = ""  # a game file (relative to the game directory) the run must not see: it runs in shadow_game_dir


def shadow_game_dir(game_dir: Path, hide: str, into: Path) -> Path:
    """A copy of `game_dir` made of links without the file `hide`: only the directories on its path are real, and
    BeachLife.ini is copied (the runner rewrites it). CI's trimmed data archive once lacked such files (run 37977122630)."""
    shutil.rmtree(into, ignore_errors=True)
    parts = Path(hide).parts

    def mirror(src: Path, dst: Path, depth: int) -> None:
        dst.mkdir(parents=True)
        for item in src.iterdir():
            if item.name.lower() == "testlogs":
                continue
            if item.name.lower() == parts[depth].lower():
                if depth + 1 < len(parts):
                    mirror(item, dst / item.name, depth + 1)
                continue
            if item.name.lower() == "beachlife.ini":
                shutil.copy2(item, dst / item.name)
            else:
                sim_workers.link_item(dst / item.name, item)

    mirror(game_dir, into, 0)
    return into


class Runner:
    def __init__(self, sim: Path, game_dir: Path, logs: Path | None = None, windowed: bool = False):
        self.sim = sim
        self.windowed = windowed
        self.game_dir = game_dir
        self.log = game_dir / "openbeach.log"
        self.ini = game_dir / "BeachLife.ini"
        self.logs = logs or game_dir / "testlogs" / "linux"
        self.logs.mkdir(parents=True, exist_ok=True)

    # --- game directory state (autotest.ps1: ini snapshot, baseline values, saves the run wrote) ---
    def _ini_set(self, values: dict) -> None:
        lines = self.ini.read_text(encoding="latin-1").splitlines() if self.ini.exists() else []
        # The game rewrites [Dialog] on exit; an absent section is the clean state. Dropped before the script's own values, so
        # an @gameini Dialog.* key (gui_dialog_rebase) survives.
        kept, skip = [], False
        for line in lines:
            m = re.match(r"^\s*\[(.*)\]\s*$", line)
            if m:
                skip = m.group(1) == "Dialog"
            if not skip:
                kept.append(line)
        lines = kept
        for (section, key), value in values.items():
            start = next((i for i, l in enumerate(lines) if l.strip().lower() == f"[{section.lower()}]"), None)
            if start is None:
                lines += [f"[{section}]", f"{key}={value}"]
                continue
            end = next((i for i in range(start + 1, len(lines)) if lines[i].lstrip().startswith("[")), len(lines))
            hit = next((i for i in range(start + 1, end) if lines[i].split("=", 1)[0].strip().lower() == key.lower()), None)
            if hit is None:
                lines.insert(end, f"{key}={value}")
            else:
                lines[hit] = f"{key}={value}"
        self.ini.write_text("\r\n".join(lines) + "\r\n", encoding="latin-1")

    def _saves(self) -> set:
        saves = self.game_dir / "SaveGames"
        if not saves.is_dir():
            saves = next((p for p in self.game_dir.iterdir() if p.name.lower() == "savegames"), saves)
        return {p for p in saves.rglob("*") if p.is_file()} if saves.is_dir() else set()

    def run(self, entry: Entry) -> tuple[int | None, str, list[str], float]:
        """Runs obl-sim once; returns (exit code or None on timeout, stdout, game log lines, seconds)."""
        snapshot = self.ini.read_bytes() if self.ini.exists() else None
        # Fresh scenario progress, as testrun.ps1: without progress.json and the legacy sp.bin that seeds it.
        progress_files = [self.game_dir / "progress.json", self.game_dir / "sp.bin"]
        progress_snapshots = {p: p.read_bytes() for p in progress_files if p.exists()}
        saves_before = self._saves()
        self._ini_set({**BASELINE_INI, **entry.gameini})
        if self.log.exists():
            self.log.unlink()
        env = dict(os.environ)
        for name, value in entry.env.items():
            env.setdefault(name, value)
        argv = [str(self.sim), "--game-dir", str(self.game_dir), "--headless", str(entry.target)]
        if self.windowed:
            # --hidden selects SDL's offscreen video driver; a runner without a sound device plays on the dummy driver.
            argv = [str(self.sim), "--game-dir", str(self.game_dir), "--script", str(entry.target), "--hidden"]
            env.setdefault("SDL_AUDIODRIVER", "dummy")
        start = time.monotonic()
        try:
            for path in progress_files:
                path.unlink(missing_ok=True)
            proc = subprocess.run(argv,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, timeout=entry.timeout)
            code, out = proc.returncode, proc.stdout.decode("utf-8", "replace")
        except subprocess.TimeoutExpired as e:
            code, out = None, (e.stdout or b"").decode("utf-8", "replace")
        finally:
            seconds = time.monotonic() - start
            for path in progress_files:
                if path in progress_snapshots:
                    path.write_bytes(progress_snapshots[path])
                else:
                    path.unlink(missing_ok=True)
            if snapshot is None:
                self.ini.unlink(missing_ok=True)
            else:
                self.ini.write_bytes(snapshot)
            for path in self._saves() - saves_before:
                path.unlink(missing_ok=True)
        lines = self.log.read_text(encoding="utf-8", errors="replace").splitlines() if self.log.exists() else []
        return code, out, lines, seconds

    def keep_log(self, name: str, lines: list[str]) -> None:
        (self.logs / f"{name}.log").write_text("\n".join(lines) + "\n", encoding="utf-8")

    def oracles(self, entry: Entry) -> str:
        for program, args in entry.oracles:
            argv = [a.replace("{run}", str(self.game_dir)).replace("{repo}", str(ORACLE_ROOT)) for a in args.split()]
            if program == "python":
                program = sys.executable
            if not shutil.which(program):
                return f"check needs {program}, not installed"
            if subprocess.run([program, *argv], cwd=ORACLE_ROOT).returncode:
                return f"check failed: {program} {' '.join(argv)}"
        return ""

    def single(self, entry: Entry) -> Result:
        code, out, lines, seconds = self.run(entry)
        self.keep_log(entry.name, lines)
        detail = "timed out" if code is None else entry.check(code, out, lines)
        if not detail:
            detail = self.oracles(entry)
        if not detail and "OBL_WORLD_HASH_DIR" in entry.env:
            shutil.rmtree(entry.env["OBL_WORLD_HASH_DIR"], ignore_errors=True)
        return Result(entry.name, not detail, seconds, detail)


# --- checks: return "" for a pass, else the reason ---
def text(out: str, lines: list[str]) -> str:
    return out + "\n" + "\n".join(lines)


def replay_passes(code: int, out: str, lines: list[str]) -> str:
    """A replay (ctest headless_replay_*): every checkpoint matches and the run ends PASS."""
    all_text = text(out, lines)
    if re.search(r"FAILED|MISMATCH", all_text):
        first = next((l for l in all_text.splitlines() if re.search(r"FAILED|MISMATCH", l)), "")
        return first.strip()[:160]
    if not re.search(r"obl-sim: end ticks=\d+ .* mismatches=0 .*\n(?:.*\n)*?.*obl-sim: PASS", all_text):
        return "no `mismatches=0` end and PASS line"
    return "" if code == 0 else f"exit code {code}"


def missing_image_fails(code: int, out: str, lines: list[str]) -> str:
    # The runtime reads the converted PNG, not Data/cursor.tga. Windows paths use backslashes in diagnostics.
    report = "obl-sim: FAILED fatal-error Cannot load packs/base/images/cursor.png: cannot open packs/base/images/cursor.png"
    if code != 5:
        return f"missing image exit code {code}, expected 5"
    if report not in out.replace("\\", "/") or not any(report in line.replace("\\", "/") for line in lines):
        return "missing fatal-error report in stdout or log"
    if "obl-sim: PASS" in text(out, lines):
        return "missing image reached PASS"
    return ""


def expect(pattern: str, code_expected: int | None = None):
    """Output must match `pattern` (the ctest PASS_REGULAR_EXPRESSION) and, if given, the exit code."""
    regex = re.compile(pattern)

    def check(code: int, out: str, lines: list[str]) -> str:
        if not regex.search(out):
            return f"output does not match {pattern[:80]}"
        if code_expected is not None and code != code_expected:
            return f"exit code {code}, expected {code_expected}"
        return ""
    return check


def script_passes(code: int, out: str, lines: list[str]) -> str:
    """A test script (autotest.ps1): no FAILED/aborted line, `quit` reached, exit 0."""
    failed = [l for l in lines if SCRIPT_FAILED.search(l)]
    if failed:
        return f"{len(failed)} failure line(s): {failed[0].split('autotest:', 1)[-1].strip()[:120]}"
    if not any("autotest: completed" in l for l in lines):
        return "did not reach quit"
    return "" if code == 0 else f"exit code {code}"


def golden_matches(tsv: Path):
    """A golden hash fixture (world_hash_golden.ps1): its world-hash lines equal tests/golden-hashes/<name>.tsv."""
    rows = [l.split("\t") for l in tsv.read_text(encoding="utf-8").splitlines()[2:] if l.strip()]

    def check(code: int, out: str, lines: list[str]) -> str:
        reason = script_passes(code, out, lines)
        if reason:
            return reason
        got = [list(m.groups()[:4]) for m in (GOLDEN_LINE.search(l) for l in lines) if m]
        if len(got) != len(rows):
            return f"checkpoint count {len(got)}, reference {len(rows)}"
        for want, have in zip(rows, got):
            if want != have:
                return f"checkpoint {want[0]}: reference steps={want[1]} fields={want[2]} hash={want[3]}, " \
                       f"got steps={have[1]} fields={have[2]} hash={have[3]}"
        return ""
    return check


# --- the entries ---
def manifest_lines() -> list[list[str]]:
    rows = []
    for line in (TESTS / "manifest.txt").read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            rows.append(line.split())
    return rows


def script_header(path: Path) -> tuple[int, dict, dict, list, bool]:
    timeout, gameini, env, oracles, solo = 120, {}, {}, [], False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if m := TIMEOUT_HEADER.match(line):
            timeout = int(m.group(1))
        if m := GAMEINI_HEADER.match(line):
            gameini[(m.group(1), m.group(2))] = m.group(3)
        if m := ENV_HEADER.match(line):
            env[m.group(1)] = m.group(2)
        if m := MAXFPS_HEADER.match(line):
            env["OBL_MAXFPS"] = m.group(1)
        if m := CHECK_HEADER.match(line):
            oracles.append((m.group(1), m.group(2)))
        solo = solo or bool(SOLO_HEADER.match(line))
    return timeout, gameini, env, oracles, solo


def script_path(name: str, scratch: Path) -> Path | None:
    """tests/<name>, or for a <family>_s<n>.txt without its own file a copy of the template <family>_sN.txt with every
    `{scenario}` replaced by <n> written to `scratch` (as Resolve-TestScript in common.ps1); None when neither exists."""
    script = TESTS / name
    if script.exists():
        return script
    m = re.fullmatch(r"(.+)_s(\d+)\.txt", name)
    if not m or not (TESTS / f"{m.group(1)}_sN.txt").exists():
        return None
    script = scratch / name
    template = (TESTS / f"{m.group(1)}_sN.txt").read_text(encoding="utf-8")
    script.write_text(template.replace("{scenario}", m.group(2)), encoding="utf-8")
    return script


def golden_entries(game_dir: Path) -> list[Entry]:
    entries = []
    scratch = game_dir / "testlogs" / "linux" / "scripts"
    scratch.mkdir(parents=True, exist_ok=True)
    for row in manifest_lines():
        name = row[0]
        if not name.endswith(".golden") or "@full" in row:
            continue
        stem = name[:-len(".golden")]
        script = script_path(f"{stem}.txt", scratch)
        if not script:
            continue
        timeout, gameini, env, _, _ = script_header(script)
        # Snapshots of each checkpoint (10-35 MB a run) go to a directory of the entry, removed after a pass.
        env["OBL_WORLD_HASH_DIR"] = str(game_dir / "testlogs" / "linux" / f"world-hash-{stem}")
        entries.append(Entry(name, script, golden_matches(TESTS / "golden-hashes" / f"{stem}.tsv"), timeout, gameini, env))
    return entries


def fixed_entries() -> list[Entry]:
    """The headless ctest entries of tests/unit/CMakeLists.txt that the Linux host can run (no import trace)."""
    return [
        Entry("headless_sim", REPLAYS / "headless_s1.replay", replay_passes),
        Entry("headless_mismatch", REPLAYS / "headless_l0_mismatch.replay", expect(
            r"obl-sim: @0 sections=staff\r?\nobl-sim: @0 section staff expected=292a594b/84 actual=292a594a/84\r?\n"
            r"(?:.*\n)*obl-sim: @150 sections=unrecognized actual=world=[0-9a-f]+/\d+ economy=(?:.*\n)*"
            r"obl-sim: end ticks=150 [^\n]* checkpoints=2 mismatches=2 [^\n]*\r?\nobl-sim: FAILED"), env={"OBL_PACK": "l0"}),
        Entry("headless_refuse", UNIT / "headless_refuse.txt", expect(
            r"obl-sim: end frames=0 renders=0 passed=0 failed=\d+ refused=5 completed=0 [^\n]*\r?\nobl-sim: FAILED")),
        Entry("headless_save_fixture", UNIT / "headless_save_fixture.txt", expect(
            r"obl-sim: end [^\n]* passed=2 failed=0 refused=0 completed=1 [^\n]*\r?\nobl-sim: PASS", 0)),
        Entry("headless_save", REPLAYS / "headless_save_s1.replay", replay_passes),
        Entry("headless_save_reload", UNIT / "headless_save_reload.txt", expect(
            r"obl-sim: end [^\n]* passed=5 failed=0 refused=0 completed=1 [^\n]*\r?\nobl-sim: PASS", 0)),
        Entry("headless_biz_map", UNIT / "headless_biz_map.txt", expect(
            r"obl-sim: end [^\n]* passed=1 failed=0 refused=0 completed=1 [^\n]*\r?\nobl-sim: PASS", 0)),
        # Startup errors must reach both the caller and the log, with the headless failure exit code.
        Entry("headless_missing_image", REPLAYS / "headless_s1.replay", missing_image_fails,
              hide="packs/base/images/cursor.png"),
        Entry("headless_batch", UNIT / "headless_batch.list", expect(
            r"obl-sim: end [^\n]* passed=5 failed=0 [^\n]*\r?\nobl-sim: PASS\r?\nobl-sim: scripts=4 passed=3 failed=0 aborted=1 ")),
    ]


def replay_entries() -> list[Entry]:
    """Every reference replay (tests/replays/*.replay except the headless_* and mismatch fixtures above)."""
    # The l0_* replays were recorded on the own L0 pack.
    return [Entry(f"replay_{p.stem}", TESTS / f"replay_{p.stem}.txt", script_passes, 300)
            if "start save " in p.read_text(encoding="utf-8") else
            Entry(f"replay_{p.stem}", p, replay_passes, env={"OBL_PACK": "l0"} if p.stem.startswith("l0_") else {})
            for p in sorted(REPLAYS.glob("*.replay")) if not p.stem.startswith("headless_") and "mismatch" not in p.stem]


def bot_passes(code: int, out: str, lines: list[str]) -> str:
    """A bot run (ctest headless_bot_*): the scenario reached the outcome the run file expects."""
    # Summary lines (the leavers) may stand between the end line and the verdict.
    if not re.search(r"obl-sim: end ticks=\d+ outcome=\w+ [^\n]*\r?\n(?:obl-sim: (?!PASS|FAILED)[^\n]*\r?\n)*obl-sim: PASS", out):
        return next((l.strip()[:160] for l in out.splitlines() if "FAILED" in l or "outcome=" in l), "no end and PASS line")
    return "" if code == 0 else f"exit code {code}"


def bot_entries() -> list[Entry]:
    """Every scenario bot run file (tests/bots/*.bot, docs/architecture/bots/index.md)."""
    entries = [Entry(f"bot_{p.stem}", p, bot_passes, 900) for p in sorted((TESTS / "bots").glob("*.bot"))]
    # The own L0 pack's runs (tests/bots/l0/) play on data/packs/l0 alone: tools/ci/l0_sim_tests.sh, no bands.
    return entries + [Entry(f"l0_bot_{p.stem}", p, bot_passes, 300, env={"OBL_PACK": "l0"})
                      for p in sorted((TESTS / "bots" / "l0").glob("*.bot"))]


def chaos_passes(code: int, out: str, lines: list[str]) -> str:
    """A chaos run (ctest headless_chaos_*): every hard invariant the host checks held. The fresh-process determinism replay is
    tools/chaos_runs.py's part, not checked here."""
    if not re.search(r"obl-sim: end ticks=\d+ commands=\d+ [^\n]*\r?\nobl-sim: PASS", out):
        return next((l.strip()[:160] for l in out.splitlines() if "FAILED" in l), "no end and PASS line")
    return "" if code == 0 else f"exit code {code}"


def chaos_entries() -> list[Entry]:
    """Every chaos run file (tests/chaos/*.chaos, docs/testing/chaos-bot.md)."""
    return [Entry(f"chaos_{p.stem}", p, chaos_passes, 600) for p in sorted((TESTS / "chaos").glob("*.chaos"))]


def windows_only(name: str) -> bool:
    """A `# @windows-only` header: the script tests something only the Windows host has (SEH), obl-sim skips it."""
    path = TESTS / name
    return path.exists() and any(l.startswith("# @windows-only") for l in path.read_text(encoding="utf-8", errors="replace").splitlines())


def headless_scripts() -> list[str]:
    return [row[0] for row in manifest_lines() if "@headless" in row[1:] and not windows_only(row[0])]


def windowed_scripts(full: bool) -> list[str]:
    """The windowed game scripts of the manifest (no @headless tag) the Linux windowed host runs: `# @windows-only` and
    `# @linux-windowed-skip <reason>` scripts stay out, @full ones only with `full`."""
    names = []
    for row in manifest_lines():
        name = row[0]
        if not name.endswith(".txt") or "@headless" in row[1:] or ("@full" in row[1:] and not full) or windows_only(name):
            continue
        path = TESTS / name
        if path.exists() and any(LINUX_WINDOWED_SKIP.match(l) for l in path.read_text(encoding="utf-8", errors="replace").splitlines()):
            continue
        names.append(name)
    return names


def run_batch(runner: Runner, names: list[str], paths: dict[str, Path], index: int = 0) -> list[Result]:
    """Several @headless scripts in one process (a `.list`, docs/architecture/headless/scripts.md "Script lists"); the log is split at
    each `obl-sim: script` line. A script without its `obl-sim: end` line (a crash or hang on it or before it) runs again alone."""
    paths = {n: paths[n].resolve() for n in names}
    listing = runner.logs / f"batch-{index}.list"
    listing.write_text("\n".join(str(p) for p in paths.values()) + "\n", encoding="utf-8")
    timeout = sum(script_header(p)[0] for p in paths.values())
    code, out, lines, seconds = runner.run(Entry("batch", listing, None, timeout))
    sections: dict[str, list[str]] = {}
    current = None
    for line in lines:
        if m := re.search(r"obl-sim: script (.+)$", line):
            current = str(Path(m.group(1).strip()).resolve())
            sections[current] = []
        if current:
            sections[current].append(line)
    results = []
    for name in names:
        section = sections.get(str(paths[name]))
        if not section or not any("obl-sim: end " in l for l in section):
            print(f"  {name} did not finish in its batch - running it alone")
            results.append(runner.single(Entry(name, paths[name], script_passes, script_header(paths[name])[0])))
            continue
        runner.keep_log(Path(name).stem, section)
        verdict = script_passes(0, "", section)
        ms = next((int(m.group(1)) for l in reversed(section) if (m := re.search(r"obl-sim: end .* ms=(\d+)", l))), 0)
        results.append(Result(name, not verdict, ms / 1000, verdict))
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game-dir", default=str(REPO / "run"), help="game directory with the imported pack")
    parser.add_argument("--build-dir", default=os.environ.get("OBL_SIM_BUILD", str(REPO / "build" / "linux-sim-x64")))
    parser.add_argument("--no-build", action="store_true", help="use the obl-sim already built")
    parser.add_argument("--test-root", help="directory laid out like tests/ to take the entries from (default: this checkout's)")
    parser.add_argument("--sim", help="an already built headless host to run instead of obl-sim (no build), for example "
                        "build/x64/bin/Debug/OpenBeach.exe for a local Windows check of the same entries")
    parser.add_argument("--filter", nargs="*", default=[], help="fnmatch patterns on entry names (headless_sim, replay_s3_resort, "
                        "golden_hash_s3.golden, command_demolish.txt, ...)")
    parser.add_argument("--skip", nargs="*", default=[], help="fnmatch patterns of entries to leave out (CI: bot_* runs in "
                        "tools/bot_runs.py with every seed)")
    parser.add_argument("--solo", action="store_true", help="every @headless script in its own process (no batches)")
    parser.add_argument("--list", action="store_true", help="print the entries and exit")
    parser.add_argument("--windowed", action="store_true", help="the windowed game scripts on the windowed OpenBeach "
                        "(-DOBL_LINUX_WINDOWED=ON) instead of the headless entries")
    parser.add_argument("--full", action="store_true", help="--windowed: include @full scripts")
    parser.add_argument("--jobs", type=int, default=sim_workers.default_jobs(), help="processes at once (default: CPU count, "
                        "1 on Windows)")
    args = parser.parse_args()
    if sys.platform != "linux" and not args.sim:
        print("obl_sim_tests.py builds obl-sim on Linux (WSL on Windows); elsewhere pass --sim", file=sys.stderr)
        return 2
    if args.test_root:
        set_test_root(Path(args.test_root).resolve())
    game_dir = Path(args.game_dir).resolve()
    build_dir = Path(args.build_dir).resolve()
    sim = Path(args.sim).resolve() if args.sim else build_dir / "bin" / ("OpenBeach" if args.windowed else "obl-sim")

    if args.windowed:
        entries, scripts = [], windowed_scripts(args.full)
    else:
        entries = fixed_entries() + replay_entries() + bot_entries() + chaos_entries() + golden_entries(game_dir) if game_dir.is_dir() else []
        scripts = headless_scripts()
    wanted = lambda name: not args.filter or any(fnmatch.fnmatch(name, p) or fnmatch.fnmatch(name, p + ".txt") for p in args.filter)
    skipped = lambda name: any(fnmatch.fnmatch(name, p) for p in args.skip)
    entries = [e for e in entries if wanted(e.name) and not skipped(e.name)]
    scripts = [s for s in scripts if wanted(s) and not skipped(s)]
    if args.list:
        for name in [e.name for e in entries] + scripts:
            print(name)
        return 0
    if not (game_dir / "data").is_dir() and not any(p.name.lower() == "data" for p in game_dir.iterdir()):
        print(f"no game data in {game_dir}", file=sys.stderr)
        return 2

    if not args.no_build and not args.sim:
        env = dict(os.environ, OBL_SIM_BUILD=str(build_dir))
        build = ["bash", str(REPO / "tools" / "obl_sim_linux.sh")] + (["-DOBL_LINUX_WINDOWED=ON"] if args.windowed else [])
        if subprocess.run(build, cwd=REPO, env=env).returncode:
            print("build failed", file=sys.stderr)
            return 1
    if not sim.exists():
        print(f"no {sim.name} at {sim}", file=sys.stderr)
        return 2

    logs = game_dir / "testlogs" / ("linux-windowed" if args.windowed else "linux")
    runner = Runner(sim, game_dir, logs, args.windowed)
    start = time.monotonic()
    # Each job runs on one Runner and returns its results; `slots[i]` keeps job i's results in entry order.
    jobs: list = []

    def entry_job(entry: Entry):
        def job(r: Runner) -> list[Result]:
            if not entry.hide:
                return [r.single(entry)]
            shadow = shadow_game_dir(game_dir, entry.hide, runner.logs / f"shadow-{entry.name}")
            try:
                return [Runner(sim, shadow, runner.logs, args.windowed).single(entry)]
            finally:
                shutil.rmtree(shadow, ignore_errors=True)
        return job

    # The fixed entries share fixtures (headless_save_fixture writes testlogs/headless/save_s1.obl.save for headless_save):
    # one job runs them in order on one worker.
    fixed = {e.name for e in fixed_entries()}
    chained = [entry_job(e) for e in entries if e.name in fixed]
    if chained:
        jobs.append(lambda r: [result for job in chained for result in job(r)])
    jobs += [entry_job(e) for e in entries if e.name not in fixed]
    batch, solo, paths = [], [], {}
    scratch = game_dir / "testlogs" / "linux" / "scripts"
    scratch.mkdir(parents=True, exist_ok=True)
    for name in scripts:
        paths[name] = script_path(name, scratch) or TESTS / name
        timeout, gameini, env, oracles, needs_own = script_header(paths[name])
        if args.solo or needs_own or args.windowed:
            solo.append(Entry(name, paths[name], script_passes, timeout, gameini, env, oracles))
        else:
            batch.append(name)
    for i in range(0, len(batch), 12):  # 12 scripts per process, as runtests.ps1's batches
        jobs.append(lambda r, names=batch[i:i + 12], index=i // 12: run_batch(r, names, paths, index))
    jobs += [entry_job(e) for e in solo]

    def report(result: Result) -> None:
        print(f"{'PASS' if result.ok else 'FAIL'} {result.name} ({result.seconds:.1f} s){' - ' + result.detail if result.detail else ''}",
              flush=True)

    slots: list[list[Result]] = [[] for _ in jobs]
    workers = max(1, min(args.jobs, len(jobs)))
    if workers == 1:
        for i, job in enumerate(jobs):
            slots[i] = job(runner)
            for result in slots[i]:
                report(result)
    else:
        # The longest jobs (bots, golden runs) first would balance better, but entry order keeps logs comparable.
        pool: queue.Queue = queue.Queue()
        for w in range(workers):
            pool.put(Runner(sim, sim_workers.worker_game_dir(game_dir, runner.logs / "workers" / f"w{w}"), runner.logs, args.windowed))

        def run_job(i: int) -> int:
            r = pool.get()
            try:
                slots[i] = jobs[i](r)
            finally:
                pool.put(r)
            return i

        with concurrent.futures.ThreadPoolExecutor(workers) as executor:
            for future in concurrent.futures.as_completed([executor.submit(run_job, i) for i in range(len(jobs))]):
                for result in slots[future.result()]:
                    report(result)
        shutil.rmtree(runner.logs / "workers", ignore_errors=True)
    results = [r for slot in slots for r in slot]

    failed = [r for r in results if not r.ok]
    width = max((len(r.name) for r in results), default=10)
    print()
    print(f"{'Test':<{width}}  Result  Seconds")
    for r in results:
        print(f"{r.name:<{width}}  {'PASS' if r.ok else 'FAIL':<6}  {r.seconds:7.1f}")
    print(f"\n{len(results) - len(failed)}/{len(results)} passed in {time.monotonic() - start:.0f} s; logs in {runner.logs}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
