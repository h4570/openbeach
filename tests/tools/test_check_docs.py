"""Tests for tools/check_docs.py on synthetic doc trees (no repository files touched).

    python tests/tools/test_check_docs.py
"""
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import check_docs


def tree(files: dict[str, str]) -> Path:
    base = Path(tempfile.mkdtemp()) / "docs"
    for name, text in files.items():
        path = base / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return base


VALID = {
    "index.md": "[a](a/index.md)\n",
    "a/index.md": "[topic](topic.md) and [sub](sub/index.md), see [other](../b/x.md)\n",
    "a/topic.md": "text\n",
    "a/sub/index.md": "[part](part.md)\n",
    "a/sub/part.md": "text\n",
    "a/image.png": "not markdown, ignored\n",
}


def errors_of(files: dict[str, str]) -> list[str]:
    return check_docs.check(tree(files))[0]


def main() -> int:
    failures = 0

    def expect(name: str, condition: bool, detail) -> None:
        nonlocal failures
        print(("PASS " if condition else "FAIL ") + name + ("" if condition else f": {detail}"))
        failures += not condition

    errors = errors_of(VALID)
    expect("valid tree has no errors", errors == [], errors)

    errors = errors_of({**VALID, "loose.md": "x\n"})
    expect("loose file at the root", any("only index.md" in e for e in errors), errors)

    errors = errors_of({k: v for k, v in VALID.items() if k != "a/sub/index.md"})
    expect("directory without index", any("a/sub/: missing index.md" in e for e in errors), errors)

    errors = errors_of({**VALID, "a/unlisted.md": "x\n"})
    expect("unlisted topic", any("does not list unlisted.md" in e for e in errors), errors)

    errors = errors_of({**VALID, "a/index.md": "[topic](topic.md) [gone](gone.md) [sub](sub/index.md)\n"})
    expect("link to a missing local file", any("links to missing gone.md" in e for e in errors), errors)

    errors, warnings = check_docs.check(tree({**VALID, "a/topic.md": "line\n" * 301}))
    expect("oversized topic is a warning only", errors == [] and any("topic.md" in w for w in warnings), (errors, warnings))

    errors, warnings = check_docs.check(tree({"index.md": "[p](plan/index.md)\n", "plan/index.md": "[d](done.md)\n",
                                              "plan/done.md": "line\n" * 1000}))
    expect("plan/done.md is exempt from the size limit", errors == [] and warnings == [], (errors, warnings))

    print(f"{'PASS' if failures == 0 else 'FAIL'} ({failures} failures)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
