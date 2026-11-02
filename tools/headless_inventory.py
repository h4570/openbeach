#!/usr/bin/env python3
"""List why the game entries of tests/manifest.txt that are not @headless still need the hidden game.

Usage: python tools/headless_inventory.py [--list] [--category NAME]

Every game entry (`.txt`, `.golden`) falls into one category:
  headless      tagged @headless (runs in the headless host)
  hidden        tagged @hidden: checked to test only drawing, window, input or audio (`# Hidden-only:` in the script)
  golden        `.golden` hash tables (long runs in the hidden game; the reference replays retire them)
  replay        `replay` directive: the windowed half of a windowed/headless replay pair, kept hidden by design
  input, gui, frame, window, audio
                the first directive the headless host refuses (RefusedHeadless in src/testing/autotest.cpp, read from the
                source) belongs to this family
  map-view      reads a renderer-only probe (`map-hash*`, `map-blocks`)
  candidate     none of the above: untagged and nothing refused, so it may run headless (try `autotest.ps1 -Headless`)
Without options it prints the count per category; --list adds the entries, --category limits the list to one category.
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "tests"
AUTOTEST = ROOT / "src" / "testing" / "autotest.cpp"

# Family of each refused directive; a refused name missing here is reported as `refused`.
FAMILIES = {
    "input": ["move", "down", "up", "click", "move-client", "click-client", "relative-motion", "input-focus", "cursor-contract",
              "keyboard-contract", "input-object-contract", "object-select"],
    "gui": ["gui-"],
    "frame": ["shot", "expect-shot", "res", "display-", "graphics-modules"],
    "window": ["window-", "close-window", "fatal-error", "show-error-exit", "sdl-", "posted-"],
    "audio": ["music-", "sample-", "samples-", "spatial-", "mp3-scan", "id3-read", "sound-bank-contract"],
}
ORDER = ["headless", "hidden", "golden", "replay", "input", "gui", "frame", "window", "audio", "map-view", "refused", "candidate"]


def refused_names():
    """kExact and kPrefixes of RefusedHeadless, so the inventory follows the host."""
    text = AUTOTEST.read_text(encoding="utf-8")
    body = re.search(r"bool RefusedHeadless\(.*?\n\}", text, re.S)
    if not body:
        sys.exit(f"RefusedHeadless not found in {AUTOTEST}")
    exact = re.search(r"kExact\[\]\s*=\s*\{(.*?)\};", body.group(0), re.S)
    prefixes = re.search(r"kPrefixes\[\]\s*=\s*\{(.*?)\};", body.group(0), re.S)
    return re.findall(r'"([^"]+)"', exact.group(1)), re.findall(r'"([^"]+)"', prefixes.group(1))


def family_of(name):
    for family, names in FAMILIES.items():
        for entry in names:
            if name == entry or (entry.endswith("-") and name.startswith(entry)):
                return family
    return "refused"


def script_of(name):
    path = TESTS / name
    if not path.exists():
        template = re.match(r"^(.+_s)\d+\.txt$", name)
        if template and (TESTS / f"{template.group(1)}N.txt").exists():
            path = TESTS / f"{template.group(1)}N.txt"
    return path if path.exists() else None


def classify(name, tags, exact, prefixes):
    if "@headless" in tags:
        return "headless", ""
    if "@hidden" in tags:
        script = script_of(name)
        reason = ""
        if script:
            for line in script.read_text(encoding="utf-8", errors="replace").splitlines():
                if line.startswith("# Hidden-only:"):
                    reason = line[len("# Hidden-only:"):].strip()
                    break
        return "hidden", reason
    if name.endswith(".golden"):
        return "golden", ""
    script = script_of(name)
    if not script:
        return "candidate", "script not found"
    for number, line in enumerate(script.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        words = line.split()
        if not words or words[0].startswith("#"):
            continue
        directive = words[0]
        if directive == "replay":
            return "replay", f"line {number}"
        if directive in exact or any(directive.startswith(p) for p in prefixes):
            return family_of(directive), f"`{directive}` line {number}"
        if any(w == "map-blocks" or w.startswith("map-hash") for w in words[1:]):
            return "map-view", f"`{words[1] if len(words) > 1 else ''}` line {number}"
    return "candidate", ""


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--list", action="store_true", help="list the entries of each category")
    parser.add_argument("--category", help="list only this category")
    args = parser.parse_args()
    exact, prefixes = refused_names()
    groups = defaultdict(list)
    for raw in (TESTS / "manifest.txt").read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        name, tags = parts[0], set(parts[1:])
        if name.endswith(".ctest"):
            continue
        category, why = classify(name, tags, exact, prefixes)
        groups[category].append((name, why))
    total = sum(len(v) for v in groups.values())
    print(f"game entries: {total}")
    for category in ORDER:
        if groups.get(category):
            print(f"  {category:<10} {len(groups[category])}")
    if args.list or args.category:
        for category in ORDER:
            if args.category and category != args.category:
                continue
            for name, why in groups.get(category, []):
                print(f"{category:<10} {name}{'  ' + why if why else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
