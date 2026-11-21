import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "ci"))
import android_smoke
import obl_android


def make_elf64(e_machine, p_align):
    # Minimal 64-bit little-endian ELF header (64 bytes) + one PT_LOAD program header (56 bytes).
    ehdr = bytearray(64)
    ehdr[0:4] = b"\x7fELF"
    ehdr[4] = 2  # ELFCLASS64
    ehdr[5] = 1  # ELFDATA2LSB
    struct.pack_into("<H", ehdr, 18, e_machine)
    struct.pack_into("<Q", ehdr, 32, 64)  # e_phoff
    struct.pack_into("<HH", ehdr, 54, 56, 1)  # e_phentsize, e_phnum
    phdr = bytearray(56)
    struct.pack_into("<I", phdr, 0, android_smoke.PT_LOAD)
    struct.pack_into("<Q", phdr, 48, p_align)
    return bytes(ehdr + phdr)


class AndroidBuildTests(unittest.TestCase):
    def test_parse_version(self):
        self.assertEqual(obl_android.parse_version("1.12.5"), ("1.12.5", 11205))
        self.assertEqual(obl_android.parse_version("v1.12.5-dev.abc1234"), ("1.12.5-dev.abc1234", 11205))
        with self.assertRaises(ValueError):
            obl_android.parse_version("not-a-version")

    def test_manifest_declares_uses_sdk_matching_build_script(self):
        manifest = (ROOT / "tools" / "android" / "AndroidManifest.xml").read_text(encoding="utf-8")
        self.assertIn(f'android:minSdkVersion="{obl_android.MIN_SDK}"', manifest)
        self.assertIn(f'android:targetSdkVersion="{obl_android.TARGET_SDK}"', manifest)

    def test_stage_resources_copies_strings_shortcuts_and_icons(self):
        with tempfile.TemporaryDirectory() as tmp:
            res = Path(tmp) / "res"
            obl_android.stage_resources(res)
            self.assertTrue((res / "xml" / "shortcuts.xml").is_file())
            for lang in ("values", "values-pl", "values-de", "values-fr", "values-it", "values-es"):
                self.assertTrue((res / lang / "strings.xml").is_file(), lang)
            icons = list(res.glob("mipmap*/ic_launcher.png"))
            self.assertTrue(icons, "expected at least one launcher icon")

    def test_smoke_elf64_and_apk_contents(self):
        good_elf = make_elf64(android_smoke.EM_AARCH64, 16384)
        android_smoke.check_elf64_arm64_16k(good_elf)
        with self.assertRaises(ValueError):
            android_smoke.check_elf64_arm64_16k(make_elf64(android_smoke.EM_AARCH64, 4096))
        with self.assertRaises(ValueError):
            android_smoke.check_elf64_arm64_16k(make_elf64(62, 16384))  # x86_64

        with tempfile.TemporaryDirectory() as tmp:
            apk = Path(tmp) / "test.apk"
            with zipfile.ZipFile(apk, "w") as z:
                z.writestr("AndroidManifest.xml", b"binxml")
                z.writestr("classes.dex", b"dex")
                z.writestr("lib/arm64-v8a/libmain.so", good_elf)
                z.writestr("assets/l0/packs/l0/pack.json", b"{}")
                z.writestr("assets/LICENSE", b"GPL")
                z.writestr("assets/THIRD_PARTY_NOTICES.md", b"notices")
            android_smoke.check_apk_contents(apk)

            # Forbidden game data fails the smoke check.
            bad_apk = Path(tmp) / "bad.apk"
            with zipfile.ZipFile(bad_apk, "w") as z:
                z.writestr("AndroidManifest.xml", b"binxml")
                z.writestr("classes.dex", b"dex")
                z.writestr("lib/arm64-v8a/libmain.so", good_elf)
                z.writestr("assets/l0/packs/l0/pack.json", b"{}")
                z.writestr("assets/packs/base/pack.json", b"{}")
                z.writestr("assets/LICENSE", b"GPL")
                z.writestr("assets/THIRD_PARTY_NOTICES.md", b"notices")
            with self.assertRaises(ValueError):
                android_smoke.check_apk_contents(bad_apk)

    @unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "needs JDK")
    def test_java_data_package_importer(self):
        importer_java = ROOT / "tools" / "android" / "java" / "org" / "openbeach" / "game" / "DataPackageImporter.java"
        harness = """
import org.openbeach.game.DataPackageImporter;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.zip.*;

public class ImporterTest {
    private static void check(boolean cond, String msg) {
        if (!cond) throw new AssertionError(msg);
    }
    public static void main(String[] args) throws Exception {
        check("packs/base/pack.json".equals(DataPackageImporter.normalizeEntryPath("userdata/packs/base/pack.json")), "userdata prefix");
        check("packs/base/a/b.png".equals(DataPackageImporter.normalizeEntryPath("openbeach-data\\\\packs\\\\base\\\\a\\\\b.png")), "backslash");
        check("remaster/cursor_101.cur".equals(DataPackageImporter.normalizeEntryPath("remaster/cursor_101.cur")), "remaster");
        check("SaveGames/slot1.json".equals(DataPackageImporter.normalizeEntryPath("userdata/SaveGames/slot1.json")), "saves");
        check("BeachLife.ini".equals(DataPackageImporter.normalizeEntryPath("userdata/BeachLife.ini")), "ini");
        check(DataPackageImporter.normalizeEntryPath("../evil/packs/base/pack.json") == null, "traversal");
        check(DataPackageImporter.normalizeEntryPath("__MACOSX/packs/base/pack.json") == null, "macosx");
        check(DataPackageImporter.normalizeEntryPath("README.txt") == null, "unrelated");

        Path tmp = Paths.get(args[0]);
        Path userdata = tmp.resolve("userdata");
        Files.createDirectories(userdata.resolve("SaveGames"));
        Files.write(userdata.resolve("SaveGames/existing.json"), "keep-me".getBytes(StandardCharsets.UTF_8));

        Path zipPath = tmp.resolve("data.zip");
        try (ZipOutputStream z = new ZipOutputStream(Files.newOutputStream(zipPath))) {
            String[][] entries = {
                {"userdata/packs/base/pack.json", "{\\"language\\":\\"pl\\"}"},
                {"userdata/remaster/cursor_101.cur", "CUR"},
                {"userdata/SaveGames/existing.json", "overwrite-attempt"},
                {"userdata/SaveGames/from_pc.json", "pc-save"},
                {"userdata/BeachLife.ini", "[Beach Life Options]\\nVersion=1\\n[Language]\\nLanguage=5\\n"},
                {"unrelated.txt", "skip"},
            };
            for (String[] e : entries) {
                z.putNextEntry(new ZipEntry(e[0]));
                z.write(e[1].getBytes(StandardCharsets.UTF_8));
                z.closeEntry();
            }
        }

        try (InputStream in = Files.newInputStream(zipPath)) {
            int n = DataPackageImporter.importZip(in, Files.size(zipPath), userdata.toFile(), null);
            check(n == 5, "extracted count: " + n);
        }
        check(Files.isRegularFile(userdata.resolve("packs/base/pack.json")), "pack.json extracted");
        check(Files.isRegularFile(userdata.resolve("remaster/cursor_101.cur")), "remaster extracted");
        check("keep-me".equals(new String(Files.readAllBytes(userdata.resolve("SaveGames/existing.json")), StandardCharsets.UTF_8)), "existing save kept");
        check("pc-save".equals(new String(Files.readAllBytes(userdata.resolve("SaveGames/from_pc.json")), StandardCharsets.UTF_8)), "pc save merged");
        String ini = new String(Files.readAllBytes(userdata.resolve("BeachLife.ini")), StandardCharsets.UTF_8);
        check(ini.contains("Language=5") && ini.contains("ResWidth=1280"), "ini preserved + default video: " + ini);
        check(!Files.exists(tmp.resolve("userdata.staging")), "staging cleaned");
        check(!Files.exists(userdata.resolve("unrelated.txt")), "unrelated skipped");

        // Zip without packs/base/pack.json throws MissingPackException and leaves existing pack intact.
        Path badZip = tmp.resolve("bad.zip");
        try (ZipOutputStream z = new ZipOutputStream(Files.newOutputStream(badZip))) {
            z.putNextEntry(new ZipEntry("README.txt"));
            z.write("nope".getBytes(StandardCharsets.UTF_8));
            z.closeEntry();
        }
        boolean threw = false;
        try (InputStream in = Files.newInputStream(badZip)) {
            DataPackageImporter.importZip(in, Files.size(badZip), userdata.toFile(), null);
        } catch (DataPackageImporter.MissingPackException e) {
            threw = true;
        }
        check(threw, "missing pack should throw");
        check(Files.isRegularFile(userdata.resolve("packs/base/pack.json")), "existing pack preserved on failure");
    }
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            harness_java = tmp_path / "ImporterTest.java"
            harness_java.write_text(harness, encoding="utf-8")
            classes = tmp_path / "classes"
            classes.mkdir()
            subprocess.run(
                ["javac", "-encoding", "UTF-8", "-d", str(classes), str(importer_java), str(harness_java)],
                check=True,
            )
            work = tmp_path / "work"
            work.mkdir()
            subprocess.run(["java", "-cp", str(classes), "ImporterTest", str(work)], check=True)


if __name__ == "__main__":
    unittest.main()
