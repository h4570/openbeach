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
TEMPLATE_DIR = ROOT / "docs/modding/examples/catalog_template"
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

    def make_mod(self, root: Path, extra: dict) -> Path:
        mod_dir = root / "sample"
        mod_dir.mkdir()
        manifest = {"format": "obl.mod", "version": 1, "id": "sample", "title": "Sample", "mod_version": "1.0.0", "pack": "l0"}
        manifest.update(extra)
        (mod_dir / "mod.json").write_text(json.dumps(manifest), encoding="utf-8")
        return mod_dir

    def test_engine_min_is_copied_and_engine_max_is_not(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            mod_dir = self.make_mod(root, {"engine_min": "1.14.0", "engine_max": "1.20.0"})
            entry = build_catalog.build_entry(mod_dir, root / "out", "MIT")
            self.assertEqual(entry["engine_min"], "1.14.0")
            self.assertNotIn("engine_max", entry)

    def test_entry_without_engine_min_has_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            entry = build_catalog.build_entry(self.make_mod(root, {}), root / "out", "MIT")
            self.assertNotIn("engine_min", entry)

    def test_malformed_engine_min_is_refused(self):
        for bad in ("1.14", "01.14.0", "1.14.0-rc1", 114):
            with self.subTest(engine_min=bad), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                with self.assertRaises(ValueError):
                    build_catalog.build_entry(self.make_mod(root, {"engine_min": bad}), root / "out", "MIT")

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


class CatalogTemplateTests(unittest.TestCase):
    def test_template_builds_and_verifies_with_a_throwaway_key(self):
        signer = locate_signer()
        if signer is None:
            self.skipTest("obl_catalog_sign binary not built in this environment")
        url = "https://example.org/mods/catalog.json"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            out = root / "site"
            # A throwaway key like the workflow's fallback; the title and address are the ones a catalog owner passes.
            code = build_catalog.main([
                "--mods-dir", str(TEMPLATE_DIR / "mods"), "--out-dir", str(out), "--title", "Template catalog",
                "--url", url, "--signer", str(signer), "--ephemeral-key",
            ])
            self.assertEqual(code, 0)
            doc = json.loads((out / "catalog.json").read_text(encoding="utf-8"))
            self.assertEqual(doc["title"], "Template catalog")
            self.assertEqual([m["id"] for m in doc["mods"]], ["my_first_mod"])
            self.assertEqual(doc["mods"][0]["license"], "MIT")
            self.assertTrue((out / doc["mods"][0]["url"]).is_file())
            # The engine's parser accepts it for the address (build_catalog verified it; check once more on its own).
            ok = subprocess.run([str(signer), "verify", "--input", str(out / "catalog.json"), "--url", url],
                                capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stderr)
            # The ZIP on disk is the one the entry describes.
            entry = doc["mods"][0]
            self.assertEqual(hashlib.sha256((out / entry["url"]).read_bytes()).hexdigest(), entry["sha256"])

    def test_other_catalogs_have_no_official_defaults(self):
        with self.assertRaises(SystemExit):
            build_catalog.main(["--mods-dir", str(TEMPLATE_DIR / "mods"), "--out-dir", "unused"])

    def test_workflow_uses_its_own_secret_and_only_checks_without_it(self):
        workflow = (TEMPLATE_DIR / ".github/workflows/catalog.yml").read_text(encoding="utf-8")
        self.assertIn("secrets.CATALOG_SIGNING_KEY", workflow)
        self.assertNotIn("OBL_CATALOG_PRIVATE_KEY", workflow)
        self.assertIn("--ephemeral-key", workflow)


if __name__ == "__main__":
    unittest.main()
