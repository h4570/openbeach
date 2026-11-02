#!/usr/bin/env python3
"""Checks the layout rules of docs/ (docs/workflow/docs-policy.md).

    python tools/check_docs.py [--root docs]

Errors (exit code 1):
- a file other than index.md directly in docs/,
- a directory without index.md,
- a .md file or subdirectory not linked from its directory's index.md (topics the public export list excludes need
  no entry: the indices are the public navigation),
- an index.md link to a file or directory of its own directory that does not exist (broken links elsewhere are
  tools/check_doc_links.py's job).
Warnings (no effect on the exit code): topic files over the size limit (index.md and files the policy exempts).
"""
import argparse
import re
import sys
from pathlib import Path

try:
    from public_export import load_rules, side_of
except ImportError:  # the exported tree has no export list: all of its topics are public
    load_rules = None

ROOT = Path(__file__).resolve().parent.parent
MAX_LINES = 300
MAX_BYTES = 20 * 1024
# Append-only logs written by tools; Grep them, never read them whole.
SIZE_EXEMPT = {"plan/done.md"}
LINK_RE = re.compile(r"\]\(\s*<?([^)\s>#]+)")


def local_links(index: Path) -> set[str]:
    """Names of entries in index's own directory that index.md links to ("x.md", "sub/" -> "sub")."""
    names = set()
    for target in LINK_RE.findall(index.read_text(encoding="utf-8")):
        if "://" in target or target.startswith(("mailto:", "/", "..")):
            continue
        first = target.split("/", 1)[0]
        if first and first != ".":
            names.add(first)
    return names


def check(docs: Path) -> tuple[list[str], list[str]]:
    errors, warnings = [], []
    rules = load_rules(docs.parent) if load_rules else None
    for entry in sorted(docs.iterdir()):
        if entry.is_file() and entry.name != "index.md":
            errors.append(f"{entry.relative_to(docs.parent).as_posix()}: only index.md and directories belong in docs/")

    for directory in sorted([docs, *[d for d in docs.rglob("*") if d.is_dir()]]):
        rel_dir = directory.relative_to(docs.parent).as_posix()
        index = directory / "index.md"
        if not index.exists():
            errors.append(f"{rel_dir}/: missing index.md")
            continue
        linked = local_links(index)
        for entry in sorted(directory.iterdir()):
            if entry.name == "index.md" or (entry.is_file() and entry.suffix != ".md"):
                continue
            topic = entry / "index.md" if entry.is_dir() else entry
            if rules and side_of(rules, topic.relative_to(docs.parent).as_posix()) == "exclude":
                continue
            if entry.name not in linked:
                errors.append(f"{rel_dir}/index.md: does not list {entry.name}{'/' if entry.is_dir() else ''}")
        for name in sorted(linked):
            if not (directory / name).exists():
                errors.append(f"{rel_dir}/index.md: links to missing {name}")

    for md in sorted(docs.rglob("*.md")):
        rel = md.relative_to(docs).as_posix()
        if md.name == "index.md" or rel in SIZE_EXEMPT:
            continue
        data = md.read_bytes()
        lines = data.count(b"\n")
        if lines > MAX_LINES or len(data) > MAX_BYTES:
            warnings.append(f"docs/{rel}: {lines} lines, {len(data)} bytes (limit {MAX_LINES} lines / {MAX_BYTES} bytes); split it")
    return errors, warnings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=ROOT / "docs")
    args = parser.parse_args()
    errors, warnings = check(args.root.resolve())
    for line in warnings:
        print(f"warning: {line}")
    for line in errors:
        print(f"error: {line}")
    print("PASS" if not errors else f"FAIL ({len(errors)} errors)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
