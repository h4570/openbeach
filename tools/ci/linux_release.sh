#!/usr/bin/env bash
# The Linux release tarball (docs/build/linux-release.md): builds the windowed OpenBeach and obl-convert optimized,
# checks the glibc floor, runs the L0 references on the same build, strips the binaries and packs them.
#   tools/ci/linux_release.sh [extra cmake -D options...]
# Writes build/dist/openbeach-<version>-linux-x86_64.tar.gz and build/dist/openbeach-<version>-linux-x86_64-debug.tar.gz (the
# separate debug info). OBL_RELEASE_VERSION overrides version.txt; OBL_SIM_BUILD the build tree (build/linux-release).
# OBL_GLIBC_MAX is the newest glibc symbol version allowed (2.35: Ubuntu 22.04; Debian 12 has 2.36).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
version="${OBL_RELEASE_VERSION:-$(tr -d '[:space:]' < "$root/version.txt")}"
version="${version#v}"
build="${OBL_SIM_BUILD:-$root/build/linux-release}"
dist="${OBL_DIST_DIR:-$root/build/dist}"
glibc_max="${OBL_GLIBC_MAX:-2.35}"
name="openbeach-$version-linux-x86_64"

# RelWithDebInfo is -O2 like the Windows release build; the debug info goes to a separate archive. libstdc++ and libgcc
# link statically, so the newest GLIBCXX of the build machine is not a run-time requirement.
OBL_SIM_BUILD_TYPE=RelWithDebInfo OBL_SIM_SKIP_CONVERSION=1 OBL_SIM_BUILD="$build" \
    bash "$root/tools/obl_sim_linux.sh" -DOBL_LINUX_WINDOWED=ON \
    "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc" "$@"

# The world hashes are the behaviour contract: the optimized build must reproduce the L0 references.
l0="$build/l0-run"
rm -rf "$l0"
mkdir -p "$l0/packs/l0" "$l0/Data"
cp -R "$root/data/packs/l0/." "$l0/packs/l0/"
printf '[Beach Life Options]\nVersion=1\n' > "$l0/BeachLife.ini"
OBL_SOURCE_ROOT="$root" python3 "$root/tools/obl_sim_tests.py" --no-build --build-dir "$build" --game-dir "$l0" \
    --filter l0_boot l0_gameplay l0_guests l0_goal l0_builders "l0_bot_*" headless_mismatch "replay_l0_*" \
    "golden_hash_l0_*"

stage="$dist/$name"
rm -rf "$stage" "$dist/$name.tar.gz" "$dist/$name-debug.tar.gz"
mkdir -p "$stage/l0/packs/l0" "$dist/$name-debug"
for bin in OpenBeach obl-convert; do
    # Every needed glibc symbol must exist in the oldest supported glibc.
    newest=$(objdump -T "$build/bin/$bin" | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sed 's/GLIBC_//' | sort -Vu | tail -1)
    if [[ "$(printf '%s\n%s\n' "$newest" "$glibc_max" | sort -V | tail -1)" != "$glibc_max" ]]; then
        echo "error: $bin needs glibc $newest, newer than $glibc_max" >&2
        exit 1
    fi
    if objdump -T "$build/bin/$bin" | grep -q GLIBCXX_; then
        echo "error: $bin links the shared libstdc++" >&2
        exit 1
    fi
    echo "$bin: glibc $newest, needs $(readelf -d "$build/bin/$bin" | grep -oE '\[[^]]+\]' | tr -d '[]' | tr '\n' ' ')"
    objcopy --only-keep-debug "$build/bin/$bin" "$dist/$name-debug/$bin.debug"
    # --strip-all keeps .dynsym: the crash report still names the exported functions (-rdynamic).
    strip --strip-all -o "$stage/$bin" "$build/bin/$bin"
    objcopy --add-gnu-debuglink="$dist/$name-debug/$bin.debug" "$stage/$bin"
done
cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$stage/"
sed "s/@VERSION@/$version/g" "$root/tools/ci/linux_release_readme.txt" > "$stage/README.txt"
cp -R "$root/data/packs/l0/." "$stage/l0/packs/l0/"
tar_opts=(--sort=name --owner=0 --group=0 --numeric-owner --mtime="@${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct)}")
tar "${tar_opts[@]}" -C "$dist" -czf "$dist/$name.tar.gz" "$name"
tar "${tar_opts[@]}" -C "$dist" -czf "$dist/$name-debug.tar.gz" "$name-debug"
ls -l "$dist/$name.tar.gz" "$dist/$name-debug.tar.gz"
