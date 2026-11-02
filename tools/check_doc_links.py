#!/usr/bin/env python3
"""Checks relative Markdown links and anchors in docs/ and the root Markdown files.

For every `[text](target)` / `[label]: target` it verifies that the target file or directory exists
(relative to the file holding the link) and, when the target has a `#fragment` and is a Markdown
file, that the fragment is a heading anchor of it. Anchors follow GitHub's rule: lower-case the heading text, drop everything except
letters, digits, `_`, `-` and spaces, turn spaces into `-`, and number repeated headings
`-1`, `-2`... Explicit `<a name="x">` / `id="x"` anchors count too. Fragments on non-Markdown
targets (e.g. `file.cpp#L10`) and external links (`http:`, `https:`, `mailto:`) are not checked.
Links inside fenced code blocks and inline code are ignored.

Usage: python tools/check_doc_links.py [paths...]   (default: docs/ and the root *.md files)
Exit code 1 when any link is broken.
"""
import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PATHS = ["docs", *sorted(path.name for path in ROOT.glob("*.md"))]

FENCE_RE = re.compile(r"^\s{0,3}(`{3,}|~{3,})")
HEADING_RE = re.compile(r"^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$")
INLINE_CODE_RE = re.compile(r"(`+)(.+?)\1")
# [text](target "title") - text may contain one level of brackets (e.g. images or nested links)
INLINE_LINK_RE = re.compile(r"\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*(<[^>]*>|[^)\s]*)(?:\s+(?:\"[^\"]*\"|'[^']*'))?\s*\)")
REF_DEF_RE = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*(<[^>]*>|\S+)")
HTML_ANCHOR_RE = re.compile(r"""<a\s[^>]*?(?:name|id)\s*=\s*["']([^"']+)["']""", re.I)
EXTERNAL_RE = re.compile(r"^(?:[a-z][a-z0-9+.-]*:|//)", re.I)


def strip_inline(text: str) -> str:
    # Code spans are literal text (`<name>` keeps its word), so only the text outside them is stripped.
    parts = re.split(r"(`+.+?`+)", text)
    out = []
    for i, part in enumerate(parts):
        if i % 2:
            out.append(part.strip("`"))
            continue
        part = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", part)  # links/images -> their text
        part = re.sub(r"<[^>]+>", "", part)                      # inline HTML
        out.append(part.replace("*", "").replace("~~", ""))      # emphasis (underscores stay: GitHub keeps them)
    return "".join(out)


def slugify(heading: str) -> str:
    text = strip_inline(heading).strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


_anchor_cache: dict[Path, set[str]] = {}


def anchors_of(path: Path) -> set[str]:
    if path in _anchor_cache:
        return _anchor_cache[path]
    found: set[str] = set()
    counts: dict[str, int] = {}
    in_fence = None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = FENCE_RE.match(line)
        if m:
            marker = m.group(1)[0]
            if in_fence is None:
                in_fence = marker
            elif in_fence == marker:
                in_fence = None
            continue
        if in_fence:
            continue
        for a in HTML_ANCHOR_RE.findall(line):
            found.add(a)
        h = HEADING_RE.match(line)
        if h:
            slug = slugify(h.group(2))
            n = counts.get(slug, 0)
            counts[slug] = n + 1
            found.add(slug if n == 0 else f"{slug}-{n}")
    _anchor_cache[path] = found
    return found


def iter_links(path: Path):
    """Yields (line_number, target) for every link outside code."""
    in_fence = None
    for no, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        m = FENCE_RE.match(line)
        if m:
            marker = m.group(1)[0]
            if in_fence is None:
                in_fence = marker
            elif in_fence == marker:
                in_fence = None
            continue
        if in_fence:
            continue
        line = INLINE_CODE_RE.sub(lambda mm: " " * len(mm.group(0)), line)
        for mm in INLINE_LINK_RE.finditer(line):
            yield no, mm.group(1)
        mm = REF_DEF_RE.match(line)
        if mm:
            yield no, mm.group(1)


def display(path: Path) -> Path:
    try:
        return path.relative_to(ROOT)
    except ValueError:
        return path


def check_file(path: Path) -> list[str]:
    errors = []
    for no, raw in iter_links(path):
        target = raw[1:-1] if raw.startswith("<") and raw.endswith(">") else raw
        if not target or EXTERNAL_RE.match(target):
            continue
        base, _, frag = target.partition("#")
        base, frag = unquote(base), unquote(frag)
        dest = path if base == "" else (path.parent / base).resolve()
        rel = display(path)
        if base != "" and not dest.exists():
            errors.append(f"{rel}:{no}: missing file '{base}' (link '{target}')")
            continue
        if frag and dest.is_file() and dest.suffix.lower() == ".md":
            if frag not in anchors_of(dest):
                errors.append(f"{rel}:{no}: missing anchor '#{frag}' in {display(dest)} (link '{target}')")
    return errors


def collect(paths: list[str]) -> list[Path]:
    files: list[Path] = []
    for p in paths:
        full = (ROOT / p) if not Path(p).is_absolute() else Path(p)
        if full.is_dir():
            files += sorted(full.rglob("*.md"))
        elif full.is_file():
            files.append(full)
    return files


def main() -> int:
    files = collect(sys.argv[1:] or DEFAULT_PATHS)
    errors: list[str] = []
    for f in files:
        errors += check_file(f.resolve())
    for e in errors:
        print(e)
    print(f"{'FAIL' if errors else 'PASS'}: {len(files)} files, {len(errors)} broken links")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
