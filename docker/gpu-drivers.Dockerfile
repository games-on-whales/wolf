ARG BASE_IMAGE=ghcr.io/games-on-whales/base-app:edge
FROM $BASE_IMAGE
ENV DEBIAN_FRONTEND=noninteractive
ENV BUILD_ARCHITECTURE=amd64
ENV DEB_BUILD_OPTIONS=noddebs

# Intel (Quick Synk) specific:
# - libmfx Provides MSDK runtime (libmfxhw64.so.1) for 11th Gen Rocket Lake and older
# - libmfx-gen1.2 Provides VPL runtime (libmfx-gen.so.1.2) for 11th Gen Tiger Lake and newer
ARG REQUIRED_PACKAGES="va-driver-all intel-media-va-driver-non-free \
                       libmfx-gen1.2 libigfxcmrt7 \
                       libva-drm2 libva-x11-2 libvpl2"

RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    $REQUIRED_PACKAGES && \
    rm -rf /var/lib/apt/lists/*

# libmfx is not available in Ubuntu 25.04 so we are building from sources (see: https://github.com/games-on-whales/wolf/issues/221)
RUN <<_BUILD_LIBMFX
    #!/bin/bash
    set -e

    apt-get update -y
    apt-get install -y curl git build-essential cmake pkg-config \
                       libdrm-dev libva-dev libx11-dev libx11-xcb-dev libxcb-present-dev libxcb-dri3-dev

    cd /tmp
    git clone https://github.com/Intel-Media-SDK/MediaSDK msdk
    cd msdk
    git submodule init
    git pull

    # Patch to fix compilation error on modern gcc
    curl -fsSL https://patch-diff.githubusercontent.com/raw/Intel-Media-SDK/MediaSDK/pull/3005.patch | git apply -

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
    apt-get remove -y --purge curl git build-essential cmake pkg-config
    rm -rf /var/lib/apt/lists/* /tmp/*
_BUILD_LIBMFX

# Adding missing libnvrtc.so and libnvrtc-bulletins.so for Nvidia
# https://developer.download.nvidia.com/compute/cuda/redist/cuda_nvrtc/LICENSE.txt
RUN <<_ADD_NVRTC
    #!/bin/bash
    set -e

    #Extra deps
    apt-get update -y
    apt-get install -y unzip curl

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
    apt-get remove -y --purge unzip curl
    rm -rf /var/lib/apt/lists/*
_ADD_NVRTC

LABEL org.opencontainers.image.source="https://github.com/games-on-whales/wolf/"
LABEL org.opencontainers.image.description="A base image with all the required GPU drivers"
