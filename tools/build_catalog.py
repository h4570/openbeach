"""Build deterministic mod ZIP archives and a signed obl.mod_catalog index."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_URL = "https://openbeach.app/mods/catalog.json"
DEFAULT_TITLE = "OpenBeach Official Mods"
DEFAULT_LICENSE = "MIT"
DEFAULT_KEY_ENV = "OBL_CATALOG_PRIVATE_KEY"
SKIP_PARTS = {".git", "__pycache__", ".DS_Store"}
# Fixed DOS timestamp (1980-01-01 00:00:00) and Unix 0644 mode keep ZIP bytes identical across runs and platforms.
FIXED_ZIP_TIME = (1980, 1, 1, 0, 0, 0)
FIXED_EXTERNAL_ATTR = 0o100644 << 16


def build_mod_zip(mod_dir: Path, out_zip: Path) -> tuple[int, str]:
    """Pack `mod_dir` into `out_zip` deterministically under `<mod_dir.name>/`, returning (size, sha256_hex)."""
    mod_id = mod_dir.name
    files: list[tuple[str, Path]] = []
    for path in mod_dir.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(mod_dir)
        if any(part in SKIP_PARTS for part in rel.parts):
            continue
        files.append((rel.as_posix(), path))
    files.sort(key=lambda item: item[0])

    out_zip.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for rel_posix, path in files:
            info = zipfile.ZipInfo(filename=f"{mod_id}/{rel_posix}", date_time=FIXED_ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = FIXED_EXTERNAL_ATTR
            zf.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)

    data = out_zip.read_bytes()
    return len(data), hashlib.sha256(data).hexdigest()


def build_entry(mod_dir: Path, out_dir: Path, default_license: str) -> dict:
    manifest_path = mod_dir / "mod.json"
    if not manifest_path.is_file():
        raise ValueError(f"{mod_dir}: missing mod.json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    mod_id = manifest.get("id")
    if mod_id != mod_dir.name:
        raise ValueError(f"{manifest_path}: id {mod_id!r} does not match directory name {mod_dir.name!r}")
    title = manifest.get("title")
    mod_version = manifest.get("mod_version")
    if not isinstance(title, str) or not title:
        raise ValueError(f"{manifest_path}: missing title")
    if not isinstance(mod_version, str) or not mod_version:
        raise ValueError(f"{manifest_path}: missing mod_version")

    zip_name = f"{mod_id}-{mod_version}.zip"
    size, sha256 = build_mod_zip(mod_dir, out_dir / zip_name)

    entry: dict = {
        "id": mod_id,
        "title": title,
        "mod_version": mod_version,
    }
    if "pack" in manifest:
        entry["pack"] = manifest["pack"]
    elif "packs" in manifest:
        entry["packs"] = manifest["packs"]
    else:
        raise ValueError(f"{manifest_path}: missing pack or packs")

    for optional_key in ("scope", "api", "engine_min", "engine_max", "requires"):
        if optional_key in manifest:
            entry[optional_key] = manifest[optional_key]

    entry["url"] = zip_name
    entry["sha256"] = sha256
    entry["size"] = size
    entry["license"] = manifest.get("license") or default_license
    return entry


def find_signer(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if path.is_file():
            return path
        found = shutil.which(explicit)
        if found:
            return Path(found)
        raise FileNotFoundError(f"signer executable not found: {explicit}")
    candidates = [
        ROOT / "build/x64/bin/Debug/obl_catalog_sign.exe",
        ROOT / "build/x64/bin/RelWithDebInfo/obl_catalog_sign.exe",
        ROOT / "build/linux-sim-x64/bin/obl_catalog_sign",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    found = shutil.which("obl_catalog_sign")
    if found:
        return Path(found)
    raise FileNotFoundError("obl_catalog_sign not found; build it or pass --signer")


def run_mod_checks(mods: list[Path], engine: str, game_dir: Path, pack: str) -> None:
    check_mod_py = ROOT / "tools/check_mod.py"
    for mod_dir in mods:
        cmd = [
            sys.executable,
            str(check_mod_py),
            str(mod_dir),
            "--engine",
            engine,
            "--game-dir",
            str(game_dir),
            "--pack",
            pack,
        ]
        result = subprocess.run(cmd)
        if result.returncode != 0:
            raise RuntimeError(f"check_mod.py failed for {mod_dir.name} (exit {result.returncode})")


def build_catalog(
    mods_dir: Path,
    out_dir: Path,
    *,
    title: str = DEFAULT_TITLE,
    default_license: str = DEFAULT_LICENSE,
    signer: Path,
    key_env: str | None = DEFAULT_KEY_ENV,
    key_file: Path | None = None,
    ephemeral_key: bool = False,
    catalog_url: str = DEFAULT_URL,
) -> Path:
    if not mods_dir.is_dir():
        raise FileNotFoundError(f"mods directory not found: {mods_dir}")
    mod_dirs = sorted(p for p in mods_dir.iterdir() if p.is_dir() and (p / "mod.json").is_file())
    if not mod_dirs:
        raise ValueError(f"no mods found in {mods_dir}")

    out_dir.mkdir(parents=True, exist_ok=True)
    entries = [build_entry(mod_dir, out_dir, default_license) for mod_dir in mod_dirs]
    unsigned_doc = {
        "format": "obl.mod_catalog",
        "version": 1,
        "title": title,
        "mods": entries,
    }

    catalog_path = out_dir / "catalog.json"
    env = os.environ.copy()
    sign_args = [str(signer), "sign"]
    if ephemeral_key:
        env[DEFAULT_KEY_ENV] = secrets.token_hex(32)
        sign_args.extend(["--key-env", DEFAULT_KEY_ENV])
    elif key_file is not None:
        sign_args.extend(["--key-file", str(key_file)])
    else:
        sign_args.extend(["--key-env", key_env or DEFAULT_KEY_ENV])

    with tempfile.TemporaryDirectory() as tmp:
        unsigned_path = Path(tmp) / "catalog.unsigned.json"
        unsigned_path.write_text(
            json.dumps(unsigned_doc, sort_keys=True, separators=(",", ":"), ensure_ascii=False),
            encoding="utf-8",
        )
        sign_args.extend(["--input", str(unsigned_path), "--output", str(catalog_path)])
        sign_res = subprocess.run(sign_args, env=env)
        if sign_res.returncode != 0:
            raise RuntimeError(f"obl_catalog_sign sign failed (exit {sign_res.returncode})")

    verify_res = subprocess.run([str(signer), "verify", "--input", str(catalog_path), "--url", catalog_url])
    if verify_res.returncode != 0:
        raise RuntimeError(f"obl_catalog_sign verify failed (exit {verify_res.returncode})")

    return catalog_path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mods-dir", type=Path, default=ROOT / "data/mods", help="directory of official mod sources")
    parser.add_argument("--out-dir", type=Path, required=True, help="output directory for ZIPs and catalog.json")
    parser.add_argument("--title", default=DEFAULT_TITLE, help="catalog title")
    parser.add_argument("--license", default=DEFAULT_LICENSE, help="default SPDX license for entries")
    parser.add_argument("--signer", help="path to obl_catalog_sign executable")
    parser.add_argument("--key-env", default=DEFAULT_KEY_ENV, help="environment variable holding 64-hex Ed25519 seed")
    parser.add_argument("--key-file", type=Path, help="file holding 64-hex Ed25519 seed")
    parser.add_argument("--ephemeral-key", action="store_true", help="sign with a throwaway key (for PR/fork checks)")
    parser.add_argument("--url", default=DEFAULT_URL, help="catalog URL used for verification")
    parser.add_argument("--check-engine", help="run check_mod.py with this engine executable before packaging")
    parser.add_argument("--game-dir", type=Path, help="game directory for --check-engine")
    parser.add_argument("--pack", default="l0", help="pack id for --check-engine (default: l0)")
    args = parser.parse_args(argv)

    mods_dir = args.mods_dir.resolve()
    out_dir = args.out_dir.resolve()
    if args.check_engine:
        if not args.game_dir:
            parser.error("--check-engine requires --game-dir")
        mod_dirs = sorted(p for p in mods_dir.iterdir() if p.is_dir() and (p / "mod.json").is_file())
        run_mod_checks(mod_dirs, args.check_engine, args.game_dir.resolve(), args.pack)

    signer = find_signer(args.signer)
    catalog_path = build_catalog(
        mods_dir,
        out_dir,
        title=args.title,
        default_license=args.license,
        signer=signer,
        key_env=args.key_env,
        key_file=args.key_file.resolve() if args.key_file else None,
        ephemeral_key=args.ephemeral_key,
        catalog_url=args.url,
    )
    print(f"Wrote and verified {catalog_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
