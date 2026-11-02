"""Synthetic inputs of the old-save tests: an old save written by synthetic_old_save (tests/unit/old_save_builder.h)
and the smallest pack the save converter reads (pack.json, the language's text table, an empty catalog index)."""

import json
from pathlib import Path
import subprocess


def write_save(helper: Path, path: Path) -> None:
    subprocess.run([str(helper.resolve()), str(path)], check=True, timeout=30)


def write_pack(pack: Path, language: str = "en") -> None:
    (pack / "text").mkdir(parents=True)
    (pack / "catalog").mkdir()
    manifest = {"id": "beachlife-2002", "content_version": "1.0.0", "language": language}
    (pack / "pack.json").write_text(json.dumps(manifest), encoding="utf-8")
    (pack / "text" / f"{language}.json").write_text('{"entries": []}', encoding="utf-8")
    (pack / "catalog" / "index.json").write_text("{}", encoding="utf-8")
