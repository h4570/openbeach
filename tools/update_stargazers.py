#!/usr/bin/env python3
"""Draws the list of people who starred the repository as an SVG for the README.

    python tools/update_stargazers.py [--out stargazers.svg] [--repo owner/name]

The stargazers come from the GitHub API (GITHUB_TOKEN in the environment lifts the rate limit); the newest are shown by name,
the rest as a count. The file is written only when its content changed. The .github/workflows/stargazers.yml workflow runs
it when someone stars the repository and once a day (an unstar sends no event), and publishes the file on the unprotected
`stargazers-data` branch, so `main` gets no commit per star. The README embeds the file from that branch.
"""
import argparse
import html
import json
import os
import sys
import urllib.request
from pathlib import Path

SHOWN = 10  # names listed; GitHub shows every star on the repository page anyway
WIDTH = 880  # px; the README column is about this wide
LINE_CHARS = 100  # a line of text at 14px fits this many characters in WIDTH
LINE_HEIGHT = 22


def lines_for(logins: list[str]) -> list[str]:
    """The text lines of the picture, newest star first; the newest `SHOWN` are named."""
    if not logins:
        return ["No stars yet: be the first to star OpenBeach."]
    newest = logins[::-1][:SHOWN]
    more = len(logins) - len(newest)
    text = "★ Starred by " + ", ".join(newest) + (f" and {more} more" if more > 0 else "") + "."
    lines: list[str] = []
    current = ""
    for word in text.split(" "):
        if current and len(current) + 1 + len(word) > LINE_CHARS:
            lines.append(current)
            current = word
        else:
            current = f"{current} {word}" if current else word
    lines.append(current)
    return lines


def render(logins: list[str]) -> str:
    """The SVG for the stargazers; it follows the reader's light or dark theme."""
    lines = lines_for(logins)
    height = LINE_HEIGHT * len(lines) + 12
    rows = "".join(f'<text x="{WIDTH // 2}" y="{22 + i * LINE_HEIGHT}" text-anchor="middle">{html.escape(line)}</text>'
                   for i, line in enumerate(lines))
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{height}" viewBox="0 0 {WIDTH} {height}" '
            f'role="img" aria-label="Stargazers">'
            "<style>text{font:14px -apple-system,Segoe UI,Helvetica,Arial,sans-serif;fill:#57606a}"
            "@media (prefers-color-scheme:dark){text{fill:#8b949e}}</style>"
            f"{rows}</svg>\n")


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
    parser.add_argument("--out", default="stargazers.svg")
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", "h4570/openbeach"))
    args = parser.parse_args()
    path = Path(args.out)
    new = render(fetch_logins(args.repo, os.environ.get("GITHUB_TOKEN")))
    if path.exists() and path.read_text(encoding="utf-8") == new:
        print("stargazers: unchanged")
        return 0
    path.write_text(new, encoding="utf-8", newline="")
    print(f"stargazers: {path} written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
