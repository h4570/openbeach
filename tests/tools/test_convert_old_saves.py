"""First-start conversion of old saves (tools/convert_old_saves.py): a converted slot gets its obl.save and its .dat
moves to legacy/, a broken slot stays in place without output, a slot with an obl.save is left alone. The save and pack
are synthetic (old_save_fixture.py); with --game-dir holding run/SaveGames/save1.dat and a converted pack, the same
checks also run on that licensed copy (a private opt-in check)."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import convert_old_saves  # noqa: E402
import old_save_fixture  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--converter", required=True, type=Path)
    parser.add_argument("--save-helper", required=True, type=Path)
    parser.add_argument("--game-dir", type=Path)
    parser.add_argument("--launcher-helper", type=Path)
    args = parser.parse_args()

    def convert(saves, pack, converter, log):
        if not args.launcher_helper:
            return convert_old_saves.convert(saves, pack, converter, log)
        run = subprocess.run([str(args.launcher_helper), str(saves), str(pack), str(converter)],
                             capture_output=True, text=True, timeout=310)
        assert run.returncode in (0, 1), run
        lines = run.stdout.splitlines()
        for line in lines:
            log(line)
        converted = [line.removeprefix("converted=") for line in lines if line.startswith("converted=")]
        failed = [line.removeprefix("failed=") for line in lines if line.startswith("failed=")]
        assert run.returncode == bool(failed), run
        return converted, failed

    with tempfile.TemporaryDirectory(prefix="obl-old-saves-fixture-") as directory:
        root = Path(directory)
        old_save_fixture.write_save(args.save_helper, root / "save1.dat")
        old_save_fixture.write_pack(root / "pack")
        check(root / "save1.dat", root / "pack", convert, args)
    print("PASS old save conversion (synthetic)")
    if args.game_dir:
        corpus = args.game_dir / "SaveGames" / "save1.dat"
        pack = args.game_dir / "packs" / "base"
        if corpus.is_file() and (pack / "catalog" / "index.json").is_file():
            check(corpus, pack, convert, args)
            print("PASS old save conversion (licensed copy)")
        else:
            print("licensed-copy check skipped: no run/SaveGames/save1.dat or converted pack")
    return 0


def check(corpus, pack, convert, args):
    with tempfile.TemporaryDirectory(prefix="obl-old-saves-") as directory:
        saves = Path(directory) / "Save Games"
        saves.mkdir()
        shutil.copyfile(corpus, saves / "save3.dat")
        (saves / "save4.dat").write_bytes(b"not a save")
        shutil.copyfile(corpus, saves / "save5.dat")
        (saves / "save5.obl.save").write_text("{}", encoding="utf-8")
        (saves / "events.dat").write_bytes(b"not a slot")
        (saves / "legacy").mkdir()
        (saves / "legacy" / "save3.dat").write_bytes(b"an older copy")

        lines = []
        converted, failed = convert(saves, pack, args.converter.resolve(), lines.append)
        assert converted == ["save3.dat"] and failed == ["save4.dat"], (converted, failed, lines)

        document = json.loads((saves / "save3.obl.save").read_text(encoding="utf-8"))
        assert document["format"] == "obl.save" and document["header"]["converted_from"] is not None, document["header"]
        assert not (saves / "save3.dat").exists()
        assert (saves / "legacy" / "save3.2.dat").read_bytes() == corpus.read_bytes()
        assert (saves / "legacy" / "save3.dat").read_bytes() == b"an older copy"

        assert (saves / "save4.dat").read_bytes() == b"not a save"
        assert not list(saves.glob("save4.obl.save*")), list(saves.iterdir())
        assert any(line.startswith("save4.dat: not converted") for line in lines), lines

        assert (saves / "save5.dat").is_file() and (saves / "save5.obl.save").read_text(encoding="utf-8") == "{}"
        assert (saves / "events.dat").is_file()

        # A second start finds nothing left to convert but the broken slot.
        converted, failed = convert(saves, pack, args.converter.resolve(), lines.append)
        assert converted == [] and failed == ["save4.dat"], (converted, failed)
        if args.launcher_helper:
            # Launch failure and timeout both leave references and remove even a stale/unfinished partial.
            for converter, timeout in [(saves / "missing.exe", "1000"), (args.launcher_helper, "500")]:
                partial = saves / "save4.obl.save.partial"
                partial.write_text("stale", encoding="utf-8")
                run = subprocess.run([str(args.launcher_helper), str(saves), str(pack), str(converter), timeout],
                                     capture_output=True, text=True, timeout=10)
                assert run.returncode == 1 and "failed=save4.dat" in run.stdout, run
                assert not partial.exists() and (saves / "save4.dat").read_bytes() == b"not a save"
            assert "converter timed out" in run.stdout, run


if __name__ == "__main__":
    sys.exit(main())
