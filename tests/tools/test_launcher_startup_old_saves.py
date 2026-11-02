"""Exercise the real launcher/child-host startup on isolated saves, without entering the game.

The save and pack are synthetic (old_save_fixture.py): the save conversion before the game's start-up is checked, and
the start-up itself fails on the empty pack. With --game-dir holding run/SaveGames/save1.dat and a converted pack, the
same run on that licensed copy also requires the game to start (a private opt-in check)."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import old_save_fixture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--launcher", required=True, type=Path)
    parser.add_argument("--save-helper", required=True, type=Path)
    parser.add_argument("--game-dir", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="obl-launcher-fixture-") as directory:
        root = Path(directory)
        old_save_fixture.write_save(args.save_helper, root / "save1.dat")
        old_save_fixture.write_pack(root / "pack")
        check(args.launcher, root / "save1.dat", root / "pack", licensed=False)
    print("PASS launcher startup old saves (synthetic)")
    if args.game_dir:
        corpus = args.game_dir / "SaveGames" / "save1.dat"
        pack = args.game_dir / "packs" / "base"
        if corpus.is_file() and (pack / "catalog" / "index.json").is_file():
            check(args.launcher, corpus, pack, licensed=True)
            print("PASS launcher startup old saves (licensed copy)")
        else:
            print("licensed-copy check skipped: no run/SaveGames/save1.dat or converted pack")
    return 0


def check(launcher, corpus, pack, licensed):
    env = os.environ.copy()
    for name in ("OBL_SCRIPT", "OBL_HEADLESS", "OBL_PACK", "OBL_KEEP_SAVES"):
        env.pop(name, None)

    with tempfile.TemporaryDirectory(prefix="obl-launcher-saves-") as directory:
        game = Path(directory)
        saves = game / "SaveGames"
        saves.mkdir()
        (game / "packs").mkdir()
        # A read-only use of the pack, without copying game data; TemporaryDirectory removes the junction itself.
        if os.name == "nt":
            import _winapi
            _winapi.CreateJunction(str(pack.resolve()), str(game / "packs" / "selected"))
        else:
            (game / "packs" / "selected").symlink_to(pack.resolve(), target_is_directory=True)
        env["OBL_PACK"] = "selected"
        script_path = game / "empty.txt"
        script_path.write_text("", encoding="utf-8")

        # A default missing pack skips migration while the host still starts the game.
        def launch(executable=launcher, script=False, headless=False, keep=False):
            run_env = env.copy()
            if script:
                run_env["OBL_SCRIPT"] = str(script_path)
            if headless:
                run_env["OBL_HEADLESS"] = "test.replay"
            result = subprocess.run([str(executable.resolve()), "--hidden", "--no-entry"] + (["--keep-saves"] if keep else [])
                                    + ["--game-dir", str(game)],
                                    env=run_env, capture_output=True, text=True, timeout=120)
            log = game / "openbeach.log"
            return result, result.stdout + result.stderr + (log.read_text(encoding="utf-8", errors="replace") if log.exists() else "")

        shutil.copyfile(corpus, saves / "save3.dat")
        for flags in ({"script": True}, {"headless": True}):
            result, output = launch(**flags)
            assert "skipped for scripted/headless test host" in output, output
            assert result.returncode == 0 or not licensed, output
            assert (saves / "save3.dat").is_file() and not (saves / "legacy").exists()

        # --keep-saves (capture.ps1) leaves the corpus alone.
        result, output = launch(keep=True)
        assert "skipped by --keep-saves" in output, output
        assert result.returncode == 0 or not licensed, output
        assert (saves / "save3.dat").is_file() and not (saves / "legacy").exists()

        (saves / "save4.dat").write_bytes(b"broken")
        # A partial left by a killed start-up, with and without its .dat, is removed on the next start.
        (saves / "save3.obl.save.partial").write_bytes(b"half")
        (saves / "save9.obl.save.partial").write_bytes(b"half")
        result, output = launch()
        # Only a real pack starts the game; the conversion runs before the start-up either way.
        assert "start-up returned" in output, output
        assert not licensed or (result.returncode == 0 and "standalone host: ready" in output), output
        assert "save4.dat: not converted" in output, output
        document = json.loads((saves / "save3.obl.save").read_text(encoding="utf-8"))
        assert document["format"] == "obl.save"
        assert (saves / "legacy" / "save3.dat").read_bytes() == corpus.read_bytes()
        assert not (saves / "save3.dat").exists()
        assert (saves / "save4.dat").read_bytes() == b"broken"
        assert not list(saves.glob("*.partial"))

        env.pop("OBL_PACK")
        result, output = launch()
        assert "old saves: not converted: missing pack" in output and "start-up returned" in output, output
        env["OBL_PACK"] = "selected"

        # Copy only the launcher: a missing sibling converter is reported and the game still starts.
        isolated = game / "bin" / ("OpenBeach" + launcher.suffix)
        isolated.parent.mkdir()
        shutil.copy2(launcher, isolated)
        result, output = launch(isolated)
        assert "old saves: not converted: missing converter" in output and "start-up returned" in output, output
        assert (saves / "save4.dat").read_bytes() == b"broken"


if __name__ == "__main__":
    raise SystemExit(main())
