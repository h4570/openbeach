"""First-start conversion of old saves: every `save<N>.dat` in the save folder without a `save<N>.obl.save` goes through `obl-convert save`; a converted
`.dat` moves to `legacy/` inside the folder, so the folder holds only obl.save files. A failed conversion leaves its `.dat`
in place, writes no output and is reported; the exit code is 1 when any conversion failed.

    python tools/convert_old_saves.py <SaveGames dir> <pack dir> --converter <obl-convert.exe>
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

SLOT = re.compile(r"save(\d+)\.dat", re.IGNORECASE)


def old_slots(saves: Path) -> list[Path]:
    """The `save<N>.dat` files of the folder (not its subdirectories), by slot number."""
    found = [p for p in saves.iterdir() if p.is_file() and SLOT.fullmatch(p.name)]
    return sorted(found, key=lambda p: int(SLOT.fullmatch(p.name).group(1)))


def convert(saves: Path, pack: Path, converter: Path, log=print) -> tuple[list[str], list[str]]:
    """Converts every old slot without an obl.save; returns the converted and the failed file names."""
    converted, failed = [], []
    if not saves.is_dir():
        return converted, failed
    legacy = saves / "legacy"
    for old in old_slots(saves):
        number = SLOT.fullmatch(old.name).group(1)
        target = saves / f"save{number}.obl.save"
        if target.exists():
            continue  # converted before, or saved by the new game: the obl.save wins
        # Into a temporary name first: a conversion stopped half way must not look like a save.
        partial = saves / f"save{number}.obl.save.partial"
        partial.unlink(missing_ok=True)
        try:
            result = subprocess.run([str(converter), "save", str(old), str(pack), str(partial)],
                                    capture_output=True, text=True, timeout=300)
            reason = result.stderr.strip() or f"exit code {result.returncode}"
            ok = result.returncode == 0 and partial.is_file()
        except (OSError, subprocess.TimeoutExpired) as error:
            reason, ok = str(error), False
        if not ok:
            partial.unlink(missing_ok=True)
            failed.append(old.name)
            log(f"{old.name}: not converted, left in place: {reason}")
            continue
        # An older copy of the slot already there (its obl.save deleted since) is kept: the new one gets a numbered name.
        moved, copy = legacy / old.name, 1
        while moved.exists():
            copy += 1
            moved = legacy / f"save{number}.{copy}.dat"
        legacy.mkdir(exist_ok=True)
        partial.replace(target)
        old.replace(moved)
        converted.append(old.name)
        log(f"{old.name} -> {target.name}, old file in legacy/{moved.name}")
    return converted, failed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("saves", type=Path, help="the SaveGames directory")
    parser.add_argument("pack", type=Path, help="the converted pack the saves are keyed against (packs/base)")
    parser.add_argument("--converter", required=True, type=Path, help="obl-convert executable")
    args = parser.parse_args()
    converted, failed = convert(args.saves, args.pack, args.converter)
    print(f"old saves: {len(converted)} converted, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
