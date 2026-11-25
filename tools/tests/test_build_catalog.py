"""Tests for deterministic mod ZIP packaging and catalog signing/verification."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_catalog

ROOT = Path(__file__).resolve().parents[2]
MODS_DIR = ROOT / "data/mods"
# RFC 8032 Section 7.1 test vector 1 seed and public key (throwaway test key only).
TEST_SEED_HEX = "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
TEST_PUBKEY_HEX = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"


def locate_signer() -> Path | None:
    try:
        return build_catalog.find_signer(os.environ.get("OBL_CATALOG_SIGN"))
    except FileNotFoundError:
        return None


class BuildCatalogTests(unittest.TestCase):
    def test_zip_is_deterministic_and_matches_entry_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            out_a = root / "a"
            out_b = root / "b"
            entry_a = build_catalog.build_entry(MODS_DIR / "frame_counter", out_a, "MIT")
            entry_b = build_catalog.build_entry(MODS_DIR / "frame_counter", out_b, "MIT")
            self.assertEqual(entry_a, entry_b)

            zip_a = out_a / entry_a["url"]
            zip_b = out_b / entry_b["url"]
            bytes_a = zip_a.read_bytes()
            bytes_b = zip_b.read_bytes()
            self.assertEqual(bytes_a, bytes_b)
            self.assertEqual(entry_a["size"], len(bytes_a))
            self.assertEqual(entry_a["sha256"], hashlib.sha256(bytes_a).hexdigest())

            with zipfile.ZipFile(zip_a, "r") as zf:
                names = zf.namelist()
                self.assertIn("frame_counter/mod.json", names)
                for info in zf.infolist():
                    self.assertTrue(info.filename.startswith("frame_counter/"))
                    self.assertEqual(info.date_time, build_catalog.FIXED_ZIP_TIME)

    def test_sign_verify_and_determinism_with_engine_signer(self):
        signer = locate_signer()
        if signer is None:
            self.skipTest("obl_catalog_sign binary not built in this environment")

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            key_file = root / "test.key"
            key_file.write_text(TEST_SEED_HEX + "\n", encoding="utf-8")

            pubkey_res = subprocess.run(
                [str(signer), "pubkey", "--key-file", str(key_file)],
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertEqual(pubkey_res.stdout.strip(), TEST_PUBKEY_HEX)

            out_a = root / "catalog_a"
            out_b = root / "catalog_b"
            cat_a = build_catalog.build_catalog(MODS_DIR, out_a, signer=signer, key_file=key_file)
            cat_b = build_catalog.build_catalog(MODS_DIR, out_b, signer=signer, key_file=key_file)

            # Two builds with the same sources and key produce byte-identical catalogs and ZIPs.
            self.assertEqual(cat_a.read_bytes(), cat_b.read_bytes())
            doc = json.loads(cat_a.read_text(encoding="utf-8"))
            self.assertEqual(doc["format"], "obl.mod_catalog")
            self.assertEqual(doc["version"], 1)
            self.assertEqual(doc["public_key"], TEST_PUBKEY_HEX)
            self.assertEqual(len(doc["signature"]), 128)
            self.assertTrue(any(m["id"] == "frame_counter" for m in doc["mods"]))
            for mod in doc["mods"]:
                self.assertEqual((out_a / mod["url"]).read_bytes(), (out_b / mod["url"]).read_bytes())

            # Engine verifier accepts the signed catalog with the expected public key.
            ok = subprocess.run(
                [str(signer), "verify", "--input", str(cat_a), "--public-key", TEST_PUBKEY_HEX],
                capture_output=True,
                text=True,
            )
            self.assertEqual(ok.returncode, 0, ok.stderr)

            # Tampering with the catalog invalidates the signature.
            tampered = root / "tampered.json"
            doc["title"] = "Tampered Catalog"
            tampered.write_text(json.dumps(doc), encoding="utf-8")
            bad = subprocess.run(
                [str(signer), "verify", "--input", str(tampered)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(bad.returncode, 0)


if __name__ == "__main__":
    unittest.main()
