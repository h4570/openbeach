#!/usr/bin/env python3
"""Re-record the checkpoint hashes of reference replays after a world-hash schema change (docs/testing/replays.md).

    python tools/replay_rehash.py --sim build/x64/bin/Debug/OpenBeach.exe --game-dir run tests/replays/s1_commands.replay ...

Each replay runs headless; every `@T hash` line takes the hash the run reported and every listed section digest the actual
one. The commands, ticks and checkpoint set stay as they are, so this is only for a change that keeps the simulation (a new
hash definition), never for a behavior change: those re-record with `tools/replay.ps1 -Record`. `--keep T` leaves the
checkpoint at tick T alone (the hand-edited wrong checkpoints of the mismatch fixtures). After rewriting, the replay runs
again and must pass unless it keeps a checkpoint.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

CHECKPOINT = re.compile(r"^@(\d+) hash ([0-9a-f]{16})((?: \S+=[0-9a-f]{8}/\d+)*)$")
REPORTED = re.compile(r"obl-sim: @(\d+) hash=([0-9a-f]{16}) fields=\d+")
SECTION = re.compile(r"obl-sim: @(\d+) section (\S+) expected=\S+ actual=([0-9a-f]{8}/\d+)")


def parse_run(output: str):
    """Tick -> (hash, {section: digest}) from a headless replay's report lines."""
    result = {}
    for match in REPORTED.finditer(output):
        result[int(match[1])] = (match[2], {})
    for match in SECTION.finditer(output):
        result[int(match[1])][1][match[2]] = match[3]
    return result


def rewrite(text: str, run: dict, keep: set):
    """The replay text with the run's hashes; raises when a checkpoint was not reported."""
    lines = []
    for line in text.splitlines(keepends=True):
        match = CHECKPOINT.match(line.rstrip("\r\n"))
        if not match or int(match[1]) in keep:
            lines.append(line)
            continue
        tick = int(match[1])
        if tick not in run:
            raise ValueError(f"checkpoint @{tick} was not reported")
        digest, sections = run[tick]
        tokens = []
        for token in match[3].split():
            name = token.split("=", 1)[0]
            tokens.append(f"{name}={sections[name]}" if name in sections else token)
        ending = line[len(line.rstrip("\r\n")):]
        lines.append(" ".join([f"@{tick} hash {digest}"] + tokens) + ending)
    return "".join(lines)


def run_replay(sim: Path, game_dir: Path, replay: Path) -> str:
    done = subprocess.run([str(sim), "--game-dir", str(game_dir), "--headless", str(replay.resolve())], capture_output=True,
                          text=True, errors="replace")
    return done.stdout + done.stderr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sim", type=Path, required=True)
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--keep", type=int, action="append", default=[])
    parser.add_argument("replays", type=Path, nargs="+")
    args = parser.parse_args()
    failed = 0
    for replay in args.replays:
        output = run_replay(args.sim, args.game_dir, replay)
        if "obl-sim: end ticks=" not in output:
            print(f"{replay}: the replay did not finish\n{output[-2000:]}")
            failed += 1
            continue
        text = replay.read_text(encoding="utf-8")
        replay.write_bytes(rewrite(text, parse_run(output), set(args.keep)).encode("utf-8"))
        check = run_replay(args.sim, args.game_dir, replay)
        passed = "obl-sim: PASS" in check
        print(f"{replay}: {'PASS' if passed else 'rewritten, check FAILED'}")
        failed += not passed and not args.keep
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
