"""Game directories for parallel headless runs (tools/obl_sim_tests.py, tools/bot_runs.py --jobs).

A headless run writes into its game directory: BeachLife.ini (rewritten per test), openbeach.log, progress.json,
SaveGames/, replays/ (command_record), world-hashes/, captures/, testlogs/headless/ (fixtures) and *-dump.json.
Parallel runs therefore each get a worker directory made of symlinks to the prepared game directory, with those
writable items as private copies (new top-level files land in the worker directory itself). Symlinks need extra rights on Windows: there link_item falls back to junctions and hard links, but --jobs still defaults to 1.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

# Copied, not linked: the files and directories a run changes (compared ignoring case: the installation has `Data`).
COPIED_FILES = {"beachlife.ini", "openbeach.ini", "progress.json"}
COPIED_DIRS = {"savegames", "photos", "replays", "world-hashes", "captures"}
# Left out: the log is written fresh; testlogs holds the workers themselves, a worker starts with an empty one (its
# fixtures are written by an earlier entry of the same worker: tools/obl_sim_tests.py runs those in one job).
LEFT_OUT = {"openbeach.log", "testlogs"}


def default_jobs() -> int:
    return 1 if sys.platform == "win32" else max(1, os.cpu_count() or 1)


def link_item(link: Path, target: Path) -> None:
    """`link` pointing at `target`: a symlink, or without the right to create one (Windows, WinError 1314) a directory
    junction for a directory and a hard link (a copy across volumes) for a file; neither needs extra rights."""
    try:
        link.symlink_to(target)
        return
    except OSError:
        if sys.platform != "win32":
            raise
    if target.is_dir():
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], check=True, capture_output=True)
        return
    try:
        os.link(target, link)
    except OSError:
        shutil.copy2(target, link)


def worker_game_dir(game_dir: Path, into: Path) -> Path:
    """A fresh worker directory `into` mirroring `game_dir` (see the module comment)."""
    shutil.rmtree(into, ignore_errors=True)
    into.mkdir(parents=True)
    for item in game_dir.iterdir():
        name = item.name.lower()
        # A dump a run writes (gui-dump.json, ...) left in the game directory would be a link every worker writes through.
        if name in LEFT_OUT or name.endswith("-dump.json"):
            continue
        if name in COPIED_FILES and item.is_file():
            shutil.copy2(item, into / item.name)
        elif name in COPIED_DIRS and item.is_dir():
            shutil.copytree(item, into / item.name, symlinks=True)
        else:
            link_item(into / item.name, item)
    return into
