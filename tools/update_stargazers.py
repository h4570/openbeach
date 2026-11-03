#!/usr/bin/env python3
"""Keeps the list of people who starred the repository in README.md between two marker comments.

    python tools/update_stargazers.py [--readme README.md] [--repo owner/name]

The stargazers come from the GitHub API (GITHUB_TOKEN in the environment lifts the rate limit); the newest are shown by name,
the rest as a count. The README is written only when the block changed, so a run with no new star changes nothing. The
.github/workflows/stargazers.yml workflow runs it when someone stars the repository and once a day (an unstar sends no event).
"""
import argparse
import html
import json
import os
import re
import sys
import urllib.request
from pathlib import Path

START = "<!-- stargazers:start -->"
END = "<!-- stargazers:end -->"
SHOWN = 60  # names listed; GitHub shows every star on the repository page anyway


def render(logins: list[str], repo: str) -> str:
    """The block's text for the stargazers, oldest first as GitHub lists them; the newest `SHOWN` are named."""
    star = f"https://github.com/{repo}/stargazers"
    if not logins:
        return f'<p align="center">No stars yet: <a href="{star}">be the first to star OpenBeach</a>.</p>'
    newest = logins[::-1][:SHOWN]
    names = ", ".join(f'<a href="https://github.com/{html.escape(name)}">{html.escape(name)}</a>' for name in newest)
    more = len(logins) - len(newest)
    tail = f' and <a href="{star}">{more} more</a>' if more > 0 else ""
    return f'<p align="center">&#11088; Starred by {names}{tail}. <a href="{star}">Add yours</a>.</p>'


def replace_block(text: str, block: str) -> str:
    """`text` with what stands between the markers replaced; the markers must exist."""
    pattern = re.compile(re.escape(START) + r".*?" + re.escape(END), re.S)
    if not pattern.search(text):
        raise ValueError(f"README.md has no {START} ... {END} block")
    return pattern.sub(lambda _: f"{START}\n{block}\n{END}", text, count=1)


def fetch_logins(repo: str, token: str | None) -> list[str]:
    logins: list[str] = []
    page = 1
    while True:
        request = urllib.request.Request(f"https://api.github.com/repos/{repo}/stargazers?per_page=100&page={page}",
                                         headers={"Accept": "application/vnd.github+json", "User-Agent": "openbeach-stargazers"})
        if token:
            request.add_header("Authorization", f"Bearer {token}")
        with urllib.request.urlopen(request, timeout=30) as response:
            chunk = json.load(response)
        logins += [user["login"] for user in chunk]
        if len(chunk) < 100:
            return logins
        page += 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--readme", default="README.md")
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", "h4570/openbeach"))
    args = parser.parse_args()
    path = Path(args.readme)
    old = path.read_text(encoding="utf-8")
    new = replace_block(old, render(fetch_logins(args.repo, os.environ.get("GITHUB_TOKEN")), args.repo))
    if new == old:
        print("stargazers: unchanged")
        return 0
    path.write_text(new, encoding="utf-8", newline="")
    print("stargazers: README.md updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
