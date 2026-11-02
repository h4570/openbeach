"""Reject tracked files that never belong in the repository.

The game is imported from the player's own copy, so the repository holds no installation file, no imported pack and no
player data: settings, saves, archives or a browser profile. Also a marker or manifest of a converted pack fails.
"""
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

IMPORT_MARKERS = {"converted", "imported"}


def converted(path: Path) -> bool:
    try:
        value = json.loads(path.read_bytes())
    except (OSError, ValueError, UnicodeError):
        return False
    return isinstance(value, dict) and value.get("provenance") in IMPORT_MARKERS


# Files of an installation, whatever their directory.
INSTALLATION_SUFFIXES = {".exe", ".dll", ".iso", ".bik", ".cab", ".smk", ".pdb", ".gzf", ".rep", ".mdmp"}
PE_MAGIC = b"MZ"


def installation_file(path: Path) -> bool:
    if path.suffix.lower() in INSTALLATION_SUFFIXES:
        return True
    try:
        with path.open("rb") as handle:
            return handle.read(2) == PE_MAGIC and path.suffix.lower() not in {".txt", ".md", ".json", ".py"}
    except OSError:
        return False


# Player-local state: the player's working copy, imported packs, settings and saves live next to the game or in the
# browser, never in the repository or on the public site.
PLAYER_LOCAL_ROOTS = ("run/", "run-slots/", "og-data/", "out/", ".ccache/")
PLAYER_LOCAL_NAMES = {"beachlife.ini", "obl.ini"}
PLAYER_LOCAL_SUFFIXES = {".sav", ".sv2", ".dmp", ".zip", ".7z", ".rar", ".tar", ".gz", ".mdb", ".sqlite", ".ldb"}


def player_local(name: str, path: Path) -> bool:
    lower = name.lower()
    return (name.startswith(PLAYER_LOCAL_ROOTS) or path.name.lower() in PLAYER_LOCAL_NAMES
            or path.suffix.lower() in PLAYER_LOCAL_SUFFIXES or "/indexeddb/" in lower or "/leveldb/" in lower)


def violations(root: Path, tracked: list[str]) -> list[str]:
    errors = []
    for name in tracked:
        path = root / name
        if not path.is_file():
            continue
        if installation_file(path):
            errors.append(f"{name}: installation file")
            continue
        if player_local(name, path):
            errors.append(f"{name}: player-local imported data, settings or archive")
            continue
        if path.suffix.lower() == ".json" and converted(path):
            errors.append(f"{name}: converter provenance marker")
            continue
        parent = path.parent
        while parent.is_relative_to(root):
            if converted(parent / "pack.json"):
                errors.append(f"{name}: belongs to converted pack {parent.relative_to(root)}")
                break
            if parent == root:
                break
            parent = parent.parent
    return errors


def tracked_files(root: Path) -> list[str]:
    listing = subprocess.run(["git", "ls-files", "-z"], cwd=root, check=True, capture_output=True)
    return listing.stdout.decode("utf-8").split("\0")[:-1]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    root = ap.parse_args().root.resolve()
    errors = violations(root, tracked_files(root))
    for error in errors:
        print(error)
    print(f"Content: {len(errors)} violation(s)")
    return int(bool(errors))


if __name__ == "__main__":
    raise SystemExit(main())
