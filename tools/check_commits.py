#!/usr/bin/env python3
"""Checks commit subjects or a pull request title against the format in CLAUDE.md, "Commits".

    python tools/check_commits.py "feat(site): accept a dropped folder"   # the given subjects
    python tools/check_commits.py --range origin/main..HEAD               # the commits of a range
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys

TYPES = "feat|fix|perf|refactor|test|docs|build|ci|chore|revert"
SCOPES = "game|importer|site|platform|saves|tools"
SUBJECT = re.compile(rf"^(?:{TYPES})(?:\((?:{SCOPES})\))?!?: [a-z0-9`].*[^.]$")
MAX_LENGTH = 72
# Written by git or by the release tool.
EXEMPT = re.compile(r"^(Merge |Revert \"|chore\(main\): release )")
# A squash merge appends the pull request number to the title that was checked, so it does not count.
PR_SUFFIX = re.compile(r" \(#\d+\)$")


def subject_problem(subject: str) -> str | None:
    if EXEMPT.match(subject):
        return None
    if len(PR_SUFFIX.sub("", subject)) > MAX_LENGTH:
        return f"longer than {MAX_LENGTH} characters"
    if not SUBJECT.match(subject):
        return "not `type(scope): subject` in lowercase, without a trailing period"
    return None


def message_problem(message: str) -> str | None:
    return subject_problem(message.split("\n", 1)[0])


def range_messages(revisions: str) -> list[str]:
    out = subprocess.run(["git", "log", "--format=%B%x00", revisions], capture_output=True, text=True, encoding="utf-8", check=True)
    return [message.strip() for message in out.stdout.split("\0") if message.strip()]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("subjects", nargs="*")
    ap.add_argument("--range", dest="revisions", help="check the commits of this git range")
    args = ap.parse_args()
    messages = args.subjects + (range_messages(args.revisions) if args.revisions else [])
    failed = 0
    for message in messages:
        problem = message_problem(message)
        if problem:
            print(f"{message.splitlines()[0]}: {problem}")
            failed += 1
    print(f"Commits: {len(messages)} checked, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
