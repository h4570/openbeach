#!/usr/bin/env python3
"""Builds OpenBeach for Android (docs/build/android.md): libmain.so with the NDK, then an APK with the SDK's own tools, no Gradle.

    python tools/obl_android.py [--out FILE.apk] [--version VER] [--install] [--run] [--serial HOST:PORT]
        [--build-dir DIR] [--type RelWithDebInfo] [--keystore FILE --key-alias ALIAS] [-- cmake -D options]

Needs the Android SDK (ANDROID_HOME, else the default folder of Android Studio) with an NDK, build-tools and a platform, a JDK
(JAVA_HOME or javac on PATH), cmake and ninja (`pip install cmake ninja`). Result: <build-dir>/OpenBeach.apk (or --out) for
arm64-v8a, signed with --keystore / ANDROID_KEYSTORE_PATH when given, else with ~/.android/debug.keystore (made on the first build).
--install puts it on the device (adb), --run starts it.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HERE = ROOT / "tools" / "android"
PACKAGE = "org.openbeach.game"
MIN_SDK = 26  # AAudio, java.nio.file, adaptive icons
TARGET_SDK = 35
WINDOWS = os.name == "nt"
ICON_DENSITIES = (
    ("mipmap-mdpi", 48),
    ("mipmap-hdpi", 72),
    ("mipmap-xhdpi", 96),
    ("mipmap-xxhdpi", 144),
    ("mipmap-xxxhdpi", 192),
)


def newest(directory, pattern="*"):
    """The entry of `directory` with the highest version in its name (35.0.0, android-36, 27.1.12297006)."""
    entries = [path for path in directory.glob(pattern) if path.is_dir()]
    if not entries:
        sys.exit(f"error: nothing in {directory}: install it with the SDK manager")
    return max(entries, key=lambda path: [int(number) for number in re.findall(r"\d+", path.name)])


def sdk_root():
    for name in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        if os.environ.get(name):
            return Path(os.environ[name])
    for path in (Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk", Path.home() / "Android" / "Sdk",
                 Path.home() / "Library" / "Android" / "sdk"):
        if path.is_dir():
            return path
    sys.exit("error: no Android SDK: set ANDROID_HOME")


def tool(directory, name):
    """A tool of the SDK or JDK; on Windows some are .exe and some are .bat."""
    for suffix in ((".exe", ".bat", ".cmd") if WINDOWS else ("",)):
        if (directory / (name + suffix)).is_file():
            return str(directory / (name + suffix))
    found = shutil.which(name)
    if not found:
        sys.exit(f"error: no {name} in {directory} or on PATH")
    return found


def run(*command, env=None):
    subprocess.run([str(part) for part in command], check=True, env=env)


def parse_version(text):
    """versionName and monotonic integer versionCode from a version string (1.12.5-dev.abc -> 11205)."""
    name = text.strip().lstrip("v")
    match = re.match(r"(\d+)\.(\d+)\.(\d+)", name)
    if not match:
        raise ValueError(f"cannot parse semantic version from {text!r}")
    major, minor, patch = (int(group) for group in match.groups())
    return name, major * 10000 + minor * 100 + patch


def version(override=None):
    raw = override or os.environ.get("OBL_RELEASE_VERSION") or (ROOT / "version.txt").read_text(encoding="utf-8").split()[0]
    return parse_version(raw)


def stage_resources(stage_res):
    """Copies Android XML resources and generates mipmap launcher icons from assets/logo/openbeach-256.png."""
    source_res = HERE / "res"
    if source_res.is_dir():
        shutil.copytree(source_res, stage_res)
    else:
        stage_res.mkdir(parents=True)
    icon_src = ROOT / "assets" / "logo" / "openbeach-256.png"
    try:
        from PIL import Image
        with Image.open(icon_src) as img:
            rgba = img.convert("RGBA")
            for folder, size in ICON_DENSITIES:
                out_dir = stage_res / folder
                out_dir.mkdir(parents=True, exist_ok=True)
                rgba.resize((size, size), Image.Resampling.LANCZOS).save(out_dir / "ic_launcher.png", optimize=True)
    except ImportError:
        out_dir = stage_res / "mipmap"
        out_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(icon_src, out_dir / "ic_launcher.png")


def build_library(args, ndk):
    build = Path(args.build_dir)
    configure = [
        "cmake", "-S", ROOT / "src" / "host_linux", "-B", build, "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={args.type}",
        f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build' / 'cmake' / 'android.toolchain.cmake'}", "-DANDROID_ABI=arm64-v8a",
        f"-DANDROID_PLATFORM=android-{MIN_SDK}", "-DANDROID_STL=c++_static", "-DOBL_LINUX_WINDOWED=ON",
        # Devices with 16 KB pages need segments aligned to them; the NDK's default is 4 KB.
        "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON", "-DCMAKE_SHARED_LINKER_FLAGS=-s", *args.cmake_args,
    ]
    run(*configure)
    run("cmake", "--build", build, "--target", "OpenBeach")
    return build / "bin" / "libmain.so"


def debug_keystore(jdk_bin):
    keystore = Path.home() / ".android" / "debug.keystore"
    if not keystore.is_file():
        keystore.parent.mkdir(parents=True, exist_ok=True)
        run(tool(jdk_bin, "keytool"), "-genkeypair", "-keystore", keystore, "-storepass", "android", "-alias", "androiddebugkey",
            "-keypass", "android", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-dname", "CN=Android Debug,O=Android,C=US")
    return keystore


def sign_apk(args, tools, jdk_bin, aligned, apk):
    keystore = args.keystore or os.environ.get("ANDROID_KEYSTORE_PATH")
    if keystore:
        ks_path = Path(keystore)
        if not ks_path.is_file():
            sys.exit(f"error: keystore not found: {ks_path}")
        ks_pass = args.ks_pass or os.environ.get("ANDROID_KEYSTORE_PASSWORD", "")
        key_alias = args.key_alias or os.environ.get("ANDROID_KEY_ALIAS", "")
        key_pass = args.key_pass or os.environ.get("ANDROID_KEY_PASSWORD", "") or ks_pass
        if not ks_pass or not key_alias:
            sys.exit("error: release signing needs ANDROID_KEYSTORE_PASSWORD and ANDROID_KEY_ALIAS (or --ks-pass / --key-alias)")
        env = os.environ.copy()
        env["OBL_KS_PASS"] = ks_pass
        env["OBL_KEY_PASS"] = key_pass
        run(tool(tools, "apksigner"), "sign", "--ks", ks_path, "--ks-key-alias", key_alias,
            "--ks-pass", "env:OBL_KS_PASS", "--key-pass", "env:OBL_KEY_PASS", "--out", apk, aligned, env=env)
        print(f"signed {apk.name} with release keystore ({key_alias})")
    else:
        print(f"note: signing {apk.name} with debug keystore (set ANDROID_KEYSTORE_PATH for a release key)")
        run(tool(tools, "apksigner"), "sign", "--ks", debug_keystore(jdk_bin), "--ks-pass", "pass:android",
            "--key-pass", "pass:android", "--out", apk, aligned)
    run(tool(tools, "apksigner"), "verify", "--verbose", apk)


def build_apk(args, sdk, library):
    build = Path(args.build_dir)
    tools = newest(sdk / "build-tools")
    preferred_platform = sdk / "platforms" / f"android-{TARGET_SDK}" / "android.jar"
    android_jar = preferred_platform if preferred_platform.is_file() else newest(sdk / "platforms") / "android.jar"
    jdk_bin = Path(os.environ["JAVA_HOME"]) / "bin" if os.environ.get("JAVA_HOME") else Path(shutil.which("javac") or "javac").parent
    stage = build / "apk"
    shutil.rmtree(stage, ignore_errors=True)

    # Resources (strings, shortcuts, icons) and assets (the L0 demo + licences). Link first to generate R.java for javac.
    stage_resources(stage / "res")
    run(tool(tools, "aapt2"), "compile", "--dir", stage / "res", "-o", stage / "res.zip")
    shutil.copytree(ROOT / "data" / "packs" / "l0", stage / "assets" / "l0" / "packs" / "l0")
    for doc in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(ROOT / doc, stage / "assets" / doc)
    name, code = version(args.version)
    gen_java = stage / "gen"
    gen_java.mkdir(parents=True)
    unsigned = stage / "unsigned.apk"
    run(tool(tools, "aapt2"), "link", "-o", unsigned, "-I", android_jar, "--manifest", HERE / "AndroidManifest.xml",
        "--java", gen_java, "--min-sdk-version", MIN_SDK, "--target-sdk-version", TARGET_SDK,
        "--version-name", name, "--version-code", code, "-A", stage / "assets", stage / "res.zip")

    # Java: SDL's activity from its source tree (FetchContent), generated R.java and ours on top.
    sdl_java = build / "_deps" / "sdl3-src" / "android-project" / "app" / "src" / "main" / "java"
    sources = sorted(str(path) for path in [*sdl_java.rglob("*.java"), *(HERE / "java").rglob("*.java"), *gen_java.rglob("*.java")])
    classes = stage / "classes"
    classes.mkdir(parents=True)
    run(tool(jdk_bin, "javac"), "-source", "8", "-target", "8", "-bootclasspath", android_jar, "-Xlint:-options", "-nowarn",
        "-encoding", "UTF-8", "-d", classes, *sources)
    jar = stage / "classes.jar"
    with zipfile.ZipFile(jar, "w") as zipped:
        for path in sorted(classes.rglob("*.class")):
            zipped.write(path, path.relative_to(classes).as_posix())
    run(tool(tools, "d8"), "--release", "--min-api", MIN_SDK, "--lib", android_jar, "--output", stage, jar)

    with zipfile.ZipFile(unsigned, "a", zipfile.ZIP_DEFLATED) as zipped:
        zipped.write(stage / "classes.dex", "classes.dex")
        zipped.write(library, "lib/arm64-v8a/libmain.so")

    aligned = stage / "aligned.apk"
    run(tool(tools, "zipalign"), "-f", "-p", "4", unsigned, aligned)
    apk = Path(args.out).resolve() if args.out else build / "OpenBeach.apk"
    apk.parent.mkdir(parents=True, exist_ok=True)
    sign_apk(args, tools, jdk_bin, aligned, apk)
    return apk


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default=str(ROOT / "build" / "android-arm64"))
    parser.add_argument("--type", default="RelWithDebInfo")
    parser.add_argument("--version", help="override version.txt (e.g. 1.12.5-dev.abc1234)")
    parser.add_argument("--out", help="output APK path (default: <build-dir>/OpenBeach.apk)")
    parser.add_argument("--keystore", help="release keystore path (or ANDROID_KEYSTORE_PATH)")
    parser.add_argument("--ks-pass", help="keystore password (or ANDROID_KEYSTORE_PASSWORD)")
    parser.add_argument("--key-alias", help="key alias (or ANDROID_KEY_ALIAS)")
    parser.add_argument("--key-pass", help="key password (or ANDROID_KEY_PASSWORD)")
    parser.add_argument("--install", action="store_true", help="install the APK with adb")
    parser.add_argument("--run", action="store_true", help="install and start it")
    parser.add_argument("--serial", help="the adb device (adb -s), e.g. 192.168.1.10:5555")
    parser.add_argument("cmake_args", nargs="*")
    args = parser.parse_args()

    # A virtualenv with `pip install cmake ninja` keeps its tools beside the interpreter.
    os.environ["PATH"] = str(Path(sys.executable).parent) + os.pathsep + os.environ.get("PATH", "")
    args.build_dir = str(Path(args.build_dir).resolve())
    sdk = sdk_root()
    ndk = Path(os.environ["ANDROID_NDK_HOME"]) if os.environ.get("ANDROID_NDK_HOME") else newest(sdk / "ndk")
    apk = build_apk(args, sdk, build_library(args, ndk))
    print(f"OpenBeach.apk: {apk}")
    if args.install or args.run:
        adb = [tool(sdk / "platform-tools", "adb"), *(["-s", args.serial] if args.serial else [])]
        run(*adb, "install", "-r", apk)
        if args.run:
            run(*adb, "shell", "am", "start", "-n", f"{PACKAGE}/.LauncherActivity")


if __name__ == "__main__":
    main()
