#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive

apt-get update

# Install debugger
apt-get install -y gdb

# Install wayland-protocols
apt-get install -y wayland-protocols

# Build and install nvtop
apt-get install -y libdrm-dev libsystemd-dev libncurses5-dev libncursesw5-dev
cd /tmp/
git clone https://github.com/Syllo/nvtop.git
mkdir -p nvtop/build && cd nvtop/build
CXX=/usr/bin/clang++ cmake .. -DNVIDIA_SUPPORT=ON -DAMDGPU_SUPPORT=ON -DINTEL_SUPPORT=ON
cmake --build . --target install --config Release

# Setup nvidia
bash /etc/cont-init.d/30-nvidia.sh