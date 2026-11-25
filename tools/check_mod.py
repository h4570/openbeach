"""Run the engine mod checker and the repository content rules on a source directory."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys

from check_content import violations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--engine", default="OpenBeach", help="OpenBeach or obl-sim executable (default: OpenBeach on PATH)")
    parser.add_argument("--game-dir", type=Path, required=True, help="game directory containing the target pack")
    parser.add_argument("--pack", help="set OBL_PACK for this run")
    args = parser.parse_args()
    source = args.directory.resolve()
    if not source.is_dir():
        parser.error(f"no mod directory: {source}")
    files = sorted(p.relative_to(source).as_posix() for p in source.rglob("*") if ".git" not in p.relative_to(source).parts)
    errors = violations(source, files)
    for error in errors:
        print(error, file=sys.stderr)
    env = os.environ.copy()
    if args.pack:
        env["OBL_PACK"] = args.pack
    try:
        result = subprocess.run([args.engine, "--game-dir", str(args.game_dir.resolve()), "--check-mods", str(source)], env=env)
    except OSError as error:
        print(f"Cannot run {args.engine}: {error}", file=sys.stderr)
        return 2
    return result.returncode or int(bool(errors))


if __name__ == "__main__":
    raise SystemExit(main())
