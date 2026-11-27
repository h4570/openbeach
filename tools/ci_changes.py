#!/usr/bin/env python3
"""Tells CI whether a change needs the builds and tests, or touches only documentation or the browser site (docs/build/ci.md, "Documentation- and site-only changes").

    python tools/ci_changes.py --range origin/main...HEAD   # prints code=true or code=false
    python tools/ci_changes.py README.md docs/index.md      # the given paths
"""
from __future__ import annotations

import argparse
import subprocess
import sys

# Docs that the unit tests compare with the code, and example mods the game tests install.
CODE_UNDER_DOCS = ("docs/modding/reference/", "docs/modding/examples/")
# Static site sources and assets copied by tools/web_site.sh; lint runs web/site_check.cjs and web/site_pages.cjs on them.
SITE_PREFIXES = ("web/", "assets/logo/", "assets/screenshots/", "assets/social/")


def is_documentation(path: str) -> bool:
    path = path.replace("\\", "/")
    if "/" not in path:
        return path.endswith(".md") or path == "LICENSE"
    return path.startswith("docs/") and not path.startswith(CODE_UNDER_DOCS)


def is_site(path: str) -> bool:
    return path.replace("\\", "/").startswith(SITE_PREFIXES)


def needs_code_checks(paths: list[str]) -> bool:
    # Nothing to compare against (a new branch, a failed diff) runs everything.
    return not paths or not all(is_documentation(p) or is_site(p) for p in paths)


def changed_paths(rev_range: str) -> list[str]:
    out = subprocess.run(["git", "diff", "--name-only", rev_range], capture_output=True, text=True)
    if out.returncode != 0:
        print(out.stderr, file=sys.stderr)
        return []
    return [line for line in out.stdout.splitlines() if line]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--range", dest="rev_range", help="a git range whose changed files decide")
    parser.add_argument("paths", nargs="*", help="changed paths, instead of --range")
    args = parser.parse_args(argv)
    paths = changed_paths(args.rev_range) if args.rev_range else args.paths
    code = needs_code_checks(paths)
    print(f"code={'true' if code else 'false'}")
    if not code:
        print(f"documentation or site only: {len(paths)} file(s)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
