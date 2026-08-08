#!/bin/bash
set -e

# The devcontainer is built on the Fedora wolf-builder stage, so use dnf.

# Install debugger
dnf install -y gdb

# Install wayland-protocols
dnf install -y wayland-protocols-devel

# Build and install nvtop
dnf install -y libdrm-devel systemd-devel ncurses-devel
cd /tmp/
git clone https://github.com/Syllo/nvtop.git
mkdir -p nvtop/build && cd nvtop/build
CXX=/usr/bin/clang++ cmake .. -DNVIDIA_SUPPORT=ON -DAMDGPU_SUPPORT=ON -DINTEL_SUPPORT=ON
cmake --build . --target install --config Release

# Setup nvidia
bash /etc/cont-init.d/30-nvidia.sh

# Create base wolf cfg folder
mkdir -p $WOLF_CFG_FOLDER
