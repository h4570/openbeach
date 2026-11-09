#!/usr/bin/env bash
# The macOS release disk image (docs/build/releases.md, "macOS"): builds the windowed OpenBeach and obl-convert natively
# for arm64 (Apple silicon), runs the L0 references on the same build, lays out OpenBeach.app with
# tools/ci/release/stage.py, signs it ad hoc and packs it into a compressed .dmg.
#   tools/ci/macos_release.sh [extra cmake -D options...]
# Writes build/dist/openbeach-<version>-macos-arm64.dmg. OBL_RELEASE_VERSION overrides version.txt; OBL_SIM_BUILD the
# build tree (build/macos-release); OBL_SKIP_TESTS=1 skips the L0 references.
# Needs Xcode's command line tools (Apple Clang, codesign, hdiutil), CMake, Ninja and Python 3.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
version="${OBL_RELEASE_VERSION:-$(tr -d '[:space:]' < "$root/version.txt")}"
version="${version#v}"
build="${OBL_SIM_BUILD:-$root/build/macos-release}"
dist="${OBL_DIST_DIR:-$root/build/dist}"
name="openbeach-$version-macos-arm64"

# RelWithDebInfo is -O2 like the other releases. 13.3 is the oldest macOS the app starts on (LSMinimumSystemVersion):
# Apple's libc++ has the float std::to_chars from there.
OBL_SIM_BUILD_TYPE=RelWithDebInfo OBL_SIM_SKIP_CONVERSION=1 OBL_SIM_BUILD="$build" \
    bash "$root/tools/obl_sim_linux.sh" -DOBL_LINUX_WINDOWED=ON -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 "$@"

if [[ "${OBL_SKIP_TESTS:-0}" != 1 ]]; then
    # The world hashes are the behaviour contract: the arm64 build must reproduce the x64 L0 references.
    l0="$build/l0-run"
    rm -rf "$l0"
    mkdir -p "$l0/packs/l0" "$l0/Data"
    cp -R "$root/data/packs/l0/." "$l0/packs/l0/"
    printf '[Beach Life Options]\nVersion=1\n' > "$l0/BeachLife.ini"
    OBL_SOURCE_ROOT="$root" python3 "$root/tools/obl_sim_tests.py" --sim "$build/bin/obl-sim" --game-dir "$l0" \
        --filter l0_boot l0_gameplay l0_guests l0_goal l0_builders "l0_bot_*" headless_mismatch "replay_l0_*" \
        "golden_hash_l0_*"
fi

stage="$dist/$name"
stripped="$dist/$name-bin"
rm -rf "$stage" "$stripped" "$dist/$name.dmg"
mkdir -p "$stripped"
for bin in OpenBeach obl-convert; do
    lipo -info "$build/bin/$bin"
    # -x drops the local symbols only: the exported ones (-rdynamic) still name functions in a crash report.
    strip -x -o "$stripped/$bin" "$build/bin/$bin"
done
python3 "$root/tools/ci/release/stage.py" --platform macos --version "$version" --out "$stage" \
    --bin "$stripped/OpenBeach" "$stripped/obl-convert"
rm -rf "$stripped"
# No Developer ID: an ad-hoc signature, which arm64 needs to run at all, over the whole bundle. Gatekeeper still asks the
# player once to allow the app (README, "Open Anyway").
codesign --force --deep --sign - "$stage/OpenBeach.app"
codesign --verify --deep --strict --verbose=2 "$stage/OpenBeach.app"
# hdiutil on CI runners sometimes fails with "Resource busy"; a retry gets through.
for attempt in 1 2 3 4; do
    if hdiutil create -volname "OpenBeach $version" -srcfolder "$stage" -fs HFS+ -format UDZO -imagekey zlib-level=9 \
        -ov "$dist/$name.dmg"; then
        break
    fi
    [[ $attempt == 4 ]] && { echo "error: hdiutil create failed" >&2; exit 1; }
    sleep $((attempt * 5))
done
ls -l "$dist/$name.dmg"
