#!/usr/bin/env bash
# Runs the smoke test (tools/ci/linux_smoke.sh) of an unpacked Linux release inside clean distribution containers, with
# only the run-time packages its README.en.txt names plus Xvfb: proves the glibc floor and the package list.
#   tools/ci/linux_smoke_distros.sh <unpacked release directory> [image...]   (default: debian:12 ubuntu:24.04)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
dir="$(cd "$1" && pwd)"
shift
images=("$@")
[[ ${#images[@]} -gt 0 ]] || images=(debian:12 ubuntu:24.04)
# The package list of the README: the "sudo apt install" line and its continuation lines (ending in a backslash).
packages=$(awk '/sudo apt install/ { on = 1; sub(/.*apt install/, "") } on { line = $0; sub(/\\[ \t]*$/, "", line); print line; if ($0 !~ /\\[ \t]*$/) exit }' \
    "$dir/README.en.txt" | tr -s ' \n' '  ')
[[ -n "$packages" ]] || { echo "error: no package list in $dir/README.en.txt" >&2; exit 1; }

for image in "${images[@]}"; do
    echo "== $image: $packages"
    docker run --rm -v "$dir:/release:ro" -v "$root/tools/ci:/ci:ro" -e DEBIAN_FRONTEND=noninteractive "$image" bash -c "
        set -euo pipefail
        apt-get -o Acquire::Retries=3 update -q >/dev/null
        packages='$packages'
        # Ubuntu 24.04 renamed the ALSA library package (the 64-bit time_t transition), as the README says.
        if apt-cache show libasound2t64 >/dev/null 2>&1; then packages=\${packages/libasound2 /libasound2t64 }; fi
        apt-get install -y -q --no-install-recommends xvfb \$packages >/dev/null
        echo \"glibc \$(ldd --version | head -1 | grep -oE '[0-9.]+\$')\"
        bash /ci/linux_smoke.sh /release"
done
