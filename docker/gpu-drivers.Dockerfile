ARG BASE_IMAGE=ghcr.io/games-on-whales/base-app:fedora
FROM $BASE_IMAGE

# Intel VA-API / QSV drivers and VPL runtime
# intel-media-driver lives in RPM Fusion free on Fedora (not in the base repos),
# so enable it first; libvpl and mesa-va-drivers come from the main repos.
ARG REQUIRED_PACKAGES="libva libva-utils \
                       intel-media-driver \
                       libvpl \
                       mesa-va-drivers"

RUN dnf install -y \
      https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm && \
    dnf install -y --skip-unavailable $REQUIRED_PACKAGES && \
    dnf clean all

# libmfx is not available in Fedora so we build from sources (see: https://github.com/games-on-whales/wolf/issues/221)
RUN <<_BUILD_LIBMFX
    #!/bin/bash
    set -e

    dnf install -y curl git gcc gcc-c++ cmake pkg-config \
                   libdrm-devel libva-devel libX11-devel libxcb-devel libXext-devel

    cd /tmp
    git clone https://github.com/Intel-Media-SDK/MediaSDK msdk
    cd msdk
    git submodule init
    git pull

    # Patch to fix compilation error on modern gcc
    curl -fsSL https://patch-diff.githubusercontent.com/raw/Intel-Media-SDK/MediaSDK/pull/3005.patch | git apply -
    grep -q "#include <cstdint>" samples/sample_vpp/src/sample_vpp_frc_adv.cpp || \
      sed -i "/#include <algorithm>/a #include <cstdint>" samples/sample_vpp/src/sample_vpp_frc_adv.cpp

    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_WAYLAND=ON -DENABLE_X11_DRI3=ON -DENABLE_OPENCL=ON ../
    make -j$(nproc)
    make install -j$(nproc)

    # Adjust library path
    echo "/opt/intel/mediasdk/lib" >> /etc/ld.so.conf.d/msdk.conf
    echo "/opt/intel/mediasdk/plugins" >> /etc/ld.so.conf.d/msdk.conf
    ldconfig

    # Cleanup
    dnf clean all
    rm -rf /tmp/*
_BUILD_LIBMFX

# Adding missing libnvrtc.so and libnvrtc-bulletins.so for Nvidia
# https://developer.download.nvidia.com/compute/cuda/redist/cuda_nvrtc/LICENSE.txt
RUN <<_ADD_NVRTC
    #!/bin/bash
    set -e

    dnf install -y unzip curl

    cd /tmp
    curl -fsSL -o nvidia_cuda_nvrtc_linux_x86_64.whl "https://developer.download.nvidia.com/compute/redist/nvidia-cuda-nvrtc/nvidia_cuda_nvrtc-11.0.221-cp36-cp36m-linux_x86_64.whl"
    unzip -joq -d ./nvrtc nvidia_cuda_nvrtc_linux_x86_64.whl
    cd nvrtc
    chmod 755 libnvrtc*
    find . -maxdepth 1 -type f -name "*libnvrtc.so.*" -exec sh -c 'ln -snf $(basename {}) libnvrtc.so' \;
    mkdir -p /usr/local/nvidia/lib
    mv -f libnvrtc* /usr/local/nvidia/lib
    rm -rf /tmp/*

    echo "/usr/local/nvidia/lib" >> /etc/ld.so.conf.d/nvidia.conf
    echo "/usr/local/nvidia/lib64" >> /etc/ld.so.conf.d/nvidia.conf

    # Cleanup
    dnf clean all
_ADD_NVRTC

LABEL org.opencontainers.image.source="https://github.com/games-on-whales/wolf/"
LABEL org.opencontainers.image.description="A base image with all the required GPU drivers (Fedora)"
