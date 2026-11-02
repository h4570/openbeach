"""The pack CLI must preserve the INI language or require an explicit choice.

The INI checks run on a synthetic source. A source the CLI identifies needs a supported executable, so the language
guard of an identified source runs only with --game-dir (a private opt-in check on the licensed copy); without it,
convert.pack_manifest_language covers the manifest's language choice on synthetic packs."""

import argparse
from pathlib import Path
import json
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--converter", required=True, type=Path)
    parser.add_argument("--game-dir", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="obl-pack-language-") as directory:
        root = Path(directory)
        source, pack = root / "source", root / "pack"
        source.mkdir()
        (pack / "text").mkdir(parents=True)
        for language in ("fr", "pl"):
            (pack / "text" / f"{language}.json").write_text("{}", encoding="utf-8")
        manifest = pack / "pack.json"
        manifest.write_text("previous manifest", encoding="utf-8")

        def run(*options):
            return subprocess.run([str(args.converter.resolve()), "pack", str(source), str(pack), *options],
                                  capture_output=True, text=True, timeout=30)

        for setting in (None, "6", "0garbage"):
            if setting is not None:
                (source / "BeachLife.ini").write_text(f"[Language]\nLanguage={setting}\n", encoding="utf-8")
            result = run()
            assert result.returncode == 1 and "no valid Language.Language" in result.stderr, result
            assert manifest.read_text(encoding="utf-8") == "previous manifest"

        # The source identity requires these two installation files, copied without modifying the live run.
        for name in ("BeachLife.exe", "Data/Sprites/BLSprite.hdr"):
            original = args.game_dir / name if args.game_dir else None
            if not original or not original.is_file():
                print("PASS invalid INI checks; licensed-copy language guard skipped (no installation)")
                return 0
            target = source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, target)
        (source / "BeachLife.ini").write_text("[Language]\nLanguage=0\n", encoding="utf-8")
        result = run()
        assert result.returncode == 1 and "text/en.json" in result.stderr, result
        assert manifest.read_text(encoding="utf-8") == "previous manifest"
        result = run("--language", "pl")
        assert result.returncode == 0, result
        assert json.loads(manifest.read_text(encoding="utf-8"))["language"] == "pl"
    print("PASS pack CLI language guard and explicit override")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
