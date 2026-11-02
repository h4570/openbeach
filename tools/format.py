"""Formats the project's C/C++ sources with clang-format (.clang-format in the repo root).

    python tools/format.py            # rewrite files in place
    python tools/format.py --check    # list unformatted files, exit 1 if any (before commits, CI)
    python tools/format.py FILE...    # only these files

clang-format comes from PATH or from the pinned pip package (`pip install clang-format==21.1.2`); other major versions
format differently, so the version is checked. Generated files (*.gen.*) and third_party/ are left to their generators.
"""
import argparse
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
REQUIRED_MAJOR = "21"
EXTENSIONS = (".c", ".cpp", ".h", ".hpp", ".inl")


def find_clang_format() -> str:
    try:
        import clang_format  # pip package with the binary
        return clang_format.get_executable("clang-format")
    except ImportError:
        pass
    exe = shutil.which("clang-format")
    if not exe:
        sys.exit("clang-format not found: pip install clang-format==21.1.2")
    return exe


def check_version(exe: str) -> None:
    out = subprocess.run([exe, "--version"], capture_output=True, text=True, check=True).stdout
    version = out.split("version", 1)[-1].strip().split()[0]
    if not version.startswith(REQUIRED_MAJOR + "."):
        sys.exit(f"clang-format {version} found, {REQUIRED_MAJOR}.x required: pip install clang-format==21.1.2")


def tracked_sources() -> list[Path]:
    out = subprocess.run(["git", "ls-files", "-z"], cwd=REPO, capture_output=True, text=True, check=True).stdout
    files = []
    for name in out.split("\0"):
        if not name.endswith(EXTENSIONS) or name.startswith("third_party/") or ".gen." in name:
            continue
        files.append(REPO / name)
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="report unformatted files instead of rewriting them")
    parser.add_argument("files", nargs="*", type=Path)
    args = parser.parse_args()

    exe = find_clang_format()
    check_version(exe)
    files = [f.resolve() for f in args.files] if args.files else tracked_sources()

    def run(path: Path) -> tuple[Path, bool]:
        if args.check:
            result = subprocess.run([exe, "--dry-run", "-Werror", str(path)], capture_output=True)
            return path, result.returncode == 0
        subprocess.run([exe, "-i", str(path)], check=True)
        return path, True

    with ThreadPoolExecutor() as pool:
        results = list(pool.map(run, files))
    bad = [p for p, ok in results if not ok]
    for p in bad:
        print(f"unformatted: {p.relative_to(REPO).as_posix()}")
    if args.check:
        print(f"{len(files) - len(bad)}/{len(files)} files formatted")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
