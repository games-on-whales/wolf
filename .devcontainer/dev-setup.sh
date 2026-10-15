#!/bin/bash
set -e

# Dev tools (Fedora base image: use dnf, not apt)
dnf install -y gdb wayland-protocols zsh zsh-autosuggestions

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
