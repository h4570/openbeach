"""Replay the headless host with sprite JSONs and no sprite PNGs or pathtiles.ble.

Creates an isolated game directory; installation and pack inputs are hardlinked
read-only, while configuration and save/output directories belong to the fixture.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


REPO = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, default=REPO / "run")
    parser.add_argument("--launcher", type=Path, default=REPO / f"build/x64/bin/RelWithDebInfo/OpenBeach.exe")
    args = parser.parse_args()
    if not (args.game_dir / "packs/base/sprites").is_dir():
        print("SKIP: converted sprite pack is not prepared")
        return 77
    with tempfile.TemporaryDirectory(prefix="obl-sprite-metadata-") as scratch:
        game = Path(scratch)
        for source in args.game_dir.iterdir():
            if source.is_file() and source.suffix.lower() in {".exe", ".dll", ".ini"}:
                shutil.copy2(source, game / source.name)
        for name in ("Data", "remaster"):
            if not (args.game_dir / name).is_dir():
                continue
            def omit_source_sprite_files(directory, files):
                return files if Path(directory).name.lower() == "sprites" else [name for name in files if name.lower() == "pathtiles.ble"]
            shutil.copytree(args.game_dir / name, game / name, copy_function=os.link,
                            ignore=omit_source_sprite_files)
        pack = game / "packs/base"
        source_pack = args.game_dir / "packs/base"
        for source in source_pack.iterdir():
            target = pack / source.name
            if source.name == "sprites":
                target.mkdir(parents=True)
                for doc in source.glob("*.json"):
                    os.link(doc, target / doc.name)
                # Palette toning is boot data of the renderer, not sprite pixels.
                os.link(source / "palette_modifiers.png", target / "palette_modifiers.png")
            elif source.is_dir():
                shutil.copytree(source, target, copy_function=os.link)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                os.link(source, target)
        assert list((pack / "sprites").glob("*.json"))
        assert [p.name for p in (pack / "sprites").rglob("*.png")] == ["palette_modifiers.png"]
        (game / "SaveGames").mkdir()
        result = subprocess.run(
            [str(args.launcher.resolve()), "--game-dir", str(game), "--headless",
             str(REPO / "tests/replays/headless_s1.replay")],
            capture_output=True, text=True, timeout=180,
        )
        output = result.stdout + result.stderr
        if result.returncode or "obl-sim: PASS" not in output or "mismatches=0" not in output:
            print(output)
            log = game / "openbeach.log"
            if log.exists():
                print(log.read_text(errors="replace")[-6000:])
            return 1
        print("PASS headless replay without sprite PNGs, imported sprite archive or pathtiles.ble")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
