#!/usr/bin/env bash
# Build packages of the Linux release (tools/ci/linux_release.sh) on Ubuntu 22.04: Clang 18 from apt.llvm.org, the
# development headers SDL needs for its X11, Wayland, OpenGL and audio drivers (it loads the libraries at run time),
# and Xvfb, xwd and ImageMagick for the smoke test. CMake 3.28+ comes with the GitHub runner image.
set -euo pipefail

sudo apt-get -o Acquire::Retries=3 update -q
sudo apt-get install -y -q --no-install-recommends ninja-build gnupg wget lsb-release software-properties-common
wget -q https://apt.llvm.org/llvm.sh
sudo bash llvm.sh 18
rm llvm.sh
sudo apt-get install -y -q --no-install-recommends \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols libdecor-0-dev libegl-dev libgl-dev libgles-dev \
    libasound2-dev libpulse-dev libpipewire-0.3-dev libdrm-dev libgbm-dev \
    xvfb x11-apps imagemagick
