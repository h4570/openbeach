#!/usr/bin/env python3
"""clang-tidy (.clang-tidy: naming and a few bug-prone checks) on the C++ lines a change touches.

The whole code base does not follow the naming rules yet, so only added or modified
lines are reported: new code follows docs/workflow/code-style.md and old code converges as it is refactored.

    python3 tools/tidy.py [--base REV] [-j N]

--base: the revision to diff against (default: the merge base with origin/next); the working tree is compared, so
uncommitted edits count. Linux only (CI and WSL): it needs clang-tidy and two compilation databases:
  build/linux-tidy-x64/compile_commands.json  tests/unit + src/core + src/common (configured here with CMake if missing)
  build/linux-syntax/compile_commands.json    Windows-only sources, x86_64 MinGW target
    (tools/windows_syntax_linux.sh --compdb, refreshed when game/launcher/platform changes)
A changed header is checked through one translation unit that includes it directly. Exit code 1 when anything is
reported, 2 on a setup error.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UNIT_DB = ROOT / "build" / "linux-tidy-x64"
WINDOWS_DB = ROOT / "build" / "linux-syntax"
SOURCE_SUFFIXES = (".cpp", ".c", ".h", ".inl")
INCLUDE = re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]', re.M)


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True).stdout


def changed_lines(base: str) -> dict[str, list[list[int]]]:
    """Repo-relative path -> [[first, last], ...] of added or modified lines."""
    out: dict[str, list[list[int]]] = {}
    current = None
    for line in git("diff", "-U0", "--no-color", "--no-ext-diff", base, "--", "src", "importer", "tests/unit").splitlines():
        if line.startswith("+++ "):
            path = line[4:].removeprefix("b/")
            keep = path != "/dev/null" and path.endswith(SOURCE_SUFFIXES) and ".gen." not in path
            current = path if keep else None
        elif line.startswith("@@") and current:
            m = re.match(r"@@ -\S+ \+(\d+)(?:,(\d+))? @@", line)
            start, count = int(m.group(1)), int(m.group(2) or 1)
            if count:
                out.setdefault(current, []).append([start, start + count - 1])
    return out


def ensure_databases(windows: bool) -> None:
    if not (UNIT_DB / "compile_commands.json").exists():
        subprocess.run(["cmake", "-S", str(ROOT / "tests/unit/linux"), "-B", str(UNIT_DB), "-G", "Ninja",
                        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF",
                        "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"],
                       check=True, stdout=subprocess.DEVNULL)
    if windows:
        subprocess.run(["bash", str(ROOT / "tools/windows_syntax_linux.sh"), "--compdb"], check=True, stdout=subprocess.DEVNULL)


def load_units() -> dict[str, Path]:
    """Repo-relative source -> the build directory whose database compiles it."""
    units: dict[str, Path] = {}
    for db in (UNIT_DB, WINDOWS_DB):
        if not (db / "compile_commands.json").exists():
            continue
        for entry in json.loads((db / "compile_commands.json").read_text()):
            path = Path(entry["file"])
            if not path.is_absolute():
                path = Path(entry["directory"]) / path
            try:
                rel = path.resolve().relative_to(ROOT).as_posix()
            except ValueError:
                continue  # fetched dependencies
            units.setdefault(rel, db)
    return units


def include_index(units: dict[str, Path]) -> list[tuple[str, list[str]]]:
    """Each unit's #include names, tests first. Read once: from WSL every read crosses /mnt/c, and rescanning ~1000
    units per changed header kept a full-day diff busy for over 20 minutes."""
    index = []
    for unit in sorted(units, key=lambda u: (not u.startswith("tests/"), u)):
        try:
            text = (ROOT / unit).read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        index.append((unit, INCLUDE.findall(text)))
    return index


def unit_for_header(header: str, index: list[tuple[str, list[str]]]) -> str | None:
    name = Path(header).name
    full = (ROOT / header).as_posix()
    for unit, includes in index:
        if any(Path(inc).name == name and full.endswith(inc) for inc in includes):
            return unit
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", help="revision to diff against (default: merge base with origin/next)")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    tidy = shutil.which("clang-tidy")
    if not tidy:
        print("error: clang-tidy not found (apt-get install clang-tidy)", file=sys.stderr)
        return 2
    base = args.base or git("merge-base", "HEAD", "origin/next").strip()
    lines = changed_lines(base)
    if not lines:
        print("tidy: no changed C/C++ lines")
        return 0
    # Refresh the Windows database after source moves or target/flag changes.
    ensure_databases(any(path.startswith(("src/app/", "src/launcher/", "src/platform/")) for path in lines))
    units = load_units()
    index = include_index(units) if any(p not in units and p.endswith((".h", ".inl")) for p in lines) else []

    jobs: dict[str, Path] = {}
    unchecked = []
    for path in lines:
        unit = path if path in units else unit_for_header(path, index) if path.endswith((".h", ".inl")) else None
        if unit:
            jobs[unit] = units[unit]
        else:
            unchecked.append(path)
    headers = [path for path in lines if path.endswith((".h", ".inl"))]

    def unit_filter(unit: str) -> str:
        # Only the unit and the changed headers it includes directly (each changed header has such a unit): one filter
        # over every changed line of a long diff (eight rounds before a release) overflowed the argument list.
        try:
            names = {Path(inc).name for inc in INCLUDE.findall((ROOT / unit).read_text(encoding="utf-8", errors="replace"))}
        except OSError:
            names = set()
        paths = [unit] + [h for h in headers if Path(h).name in names]
        return json.dumps([{"name": path, "lines": lines[path]} for path in paths if path in lines])

    def run(unit: str) -> str:
        cmd = [tidy, "--quiet", "-p", str(jobs[unit]), f"--line-filter={unit_filter(unit)}", str(ROOT / unit)]
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        return "\n".join(l for l in result.stdout.splitlines() if re.search(r": (warning|error): ", l))

    with ThreadPoolExecutor(args.j) as pool:
        reports = sorted({l for out in pool.map(run, jobs) for l in out.splitlines() if l})
    for line in reports:
        print(line.replace(str(ROOT) + "/", ""))
    for path in unchecked:
        print(f"tidy: {path} has no translation unit here (not checked)")
    print(f"tidy: {len(lines)} changed files, {len(jobs)} translation units, {len(reports)} findings")
    return 1 if reports else 0


if __name__ == "__main__":
    sys.exit(main())
