#!/usr/bin/env python3
"""Per-file line coverage of src/common/ from a --coverage build of obl_unit_tests (tools/unit_coverage.sh).

Runs gcov in JSON mode over every .gcda in the build tree, merges the line counters of each source file across
translation units (a line is covered when any unit executed it) and writes a text table (lowest coverage first) and
a JSON report. Files under src/common/ that no unit includes are listed as "not compiled" (first), files that are
included but have no executable line (declarations, layouts) as "no code" (last).
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

BATCH = 200


def run_gcov(build: Path, gcda: list[Path]) -> list[dict]:
    docs: list[dict] = []
    decoder = json.JSONDecoder()
    for i in range(0, len(gcda), BATCH):
        batch = [str(p.resolve()) for p in gcda[i:i + BATCH]]
        proc = subprocess.run(["gcov", "--json-format", "--stdout", *batch], cwd=build, capture_output=True, text=True)
        if proc.returncode != 0:
            raise SystemExit(f"gcov failed ({proc.returncode}): {proc.stderr.strip()[:2000]}")
        out = proc.stdout
        pos = 0
        while True:
            while pos < len(out) and out[pos].isspace():
                pos += 1
            if pos >= len(out):
                break
            doc, pos = decoder.raw_decode(out, pos)
            docs.append(doc)
    return docs


def included_files(build: Path) -> set[Path]:
    """Every file some unit includes, from Ninja's dependency log (headers without executable lines included)."""
    out = subprocess.run(["ninja", "-C", str(build), "-t", "deps"], capture_output=True, text=True, check=True).stdout
    return {Path(line.strip()).resolve() for line in out.splitlines() if line.startswith(" ")}


def merge_lines(docs: list[dict], common: Path) -> dict[Path, dict[int, int]]:
    lines: dict[Path, dict[int, int]] = {}
    for doc in docs:
        cwd = Path(doc.get("current_working_directory", "."))
        for f in doc.get("files", []):
            path = Path(f["file"])
            if not path.is_absolute():
                path = cwd / path
            path = path.resolve()
            if common not in path.parents:
                continue
            counts = lines.setdefault(path, {})
            for ln in f.get("lines", []):
                n = ln["line_number"]
                counts[n] = counts.get(n, 0) + ln["count"]
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build", type=Path, required=True)
    ap.add_argument("--repo", type=Path, required=True)
    ap.add_argument("--text", type=Path, required=True)
    ap.add_argument("--json", type=Path, required=True)
    args = ap.parse_args()

    common = (args.repo / "src" / "common").resolve()
    gcda = sorted(args.build.rglob("*.gcda"))
    if not gcda:
        print("no .gcda files: did the test program run?", file=sys.stderr)
        return 1
    lines = merge_lines(run_gcov(args.build, gcda), common)
    included = included_files(args.build)

    rows = []
    sources = sorted(p.resolve() for p in common.rglob("*") if p.suffix in (".h", ".cpp"))
    for path in sources:
        rel = path.relative_to(args.repo.resolve()).as_posix()
        counts = lines.get(path)
        if counts is None:
            # Declarations only (no executable line) when some unit includes it; otherwise no unit compiles it at all.
            rows.append({"file": rel, "lines": 0, "covered": 0, "percent": None,
                         "status": "no code" if path in included else "not compiled"})
            continue
        covered = sum(1 for c in counts.values() if c > 0)
        uncovered = sorted(n for n, c in counts.items() if c == 0)
        rows.append({"file": rel, "lines": len(counts), "covered": covered,
                     "percent": 100.0 * covered / len(counts) if counts else 100.0, "uncovered_lines": uncovered})

    # Not compiled first, then lowest coverage (ties: more uncovered lines first); declaration-only files last.
    def order(r: dict) -> tuple:
        status = r.get("status")
        rank = 0 if status == "not compiled" else 2 if status == "no code" else 1
        return (rank, r["percent"] or 0.0, -(r["lines"] - r["covered"]), r["file"])
    rows.sort(key=order)
    args.json.write_text(json.dumps(rows, indent=1) + "\n", encoding="utf-8")

    total = sum(r["lines"] for r in rows)
    covered = sum(r["covered"] for r in rows)
    out = [f"{'percent':>8} {'covered':>8} {'lines':>6}  file"]
    for r in rows:
        pct = {"not compiled": "n/c", "no code": "-"}.get(r.get("status"), "") or f"{r['percent']:7.1f}%"
        out.append(f"{pct:>8} {r['covered']:>8} {r['lines']:>6}  {r['file']}")
    not_compiled = sum(1 for r in rows if r.get("status") == "not compiled")
    no_code = sum(1 for r in rows if r.get("status") == "no code")
    summary = (f"src/common: {covered}/{total} executable lines covered "
               f"({100.0 * covered / total if total else 0:.1f}%), {len(rows)} files, "
               f"{not_compiled} not compiled (n/c), {no_code} without executable lines (-)")
    out.append(summary)
    args.text.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
