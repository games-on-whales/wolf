ARG BASE_IMAGE=ghcr.io/games-on-whales/base-app:edge
FROM $BASE_IMAGE
ENV DEBIAN_FRONTEND=noninteractive
ENV BUILD_ARCHITECTURE=amd64
ENV DEB_BUILD_OPTIONS=noddebs

# Intel (Quick Synk) specific:
# - libmfx Provides MSDK runtime (libmfxhw64.so.1) for 11th Gen Rocket Lake and older
# - libmfx-gen1.2 Provides VPL runtime (libmfx-gen.so.1.2) for 11th Gen Tiger Lake and newer
ARG REQUIRED_PACKAGES="va-driver-all intel-media-va-driver-non-free \
                       libmfx1 libmfx-gen1.2 libigfxcmrt7 \
                       libva-drm2 libva-x11-2 libvpl2"

RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    $REQUIRED_PACKAGES && \
    rm -rf /var/lib/apt/lists/*

# Adding missing libnvrtc.so and libnvrtc-bulletins.so for Nvidia
# https://developer.download.nvidia.com/compute/cuda/redist/cuda_nvrtc/LICENSE.txt
RUN <<_ADD_NVRTC
    #!/bin/bash
    set -e

    #Extra deps
    apt-get update -y
    apt-get install -y xz-utils curl

    cd /tmp
    mkdir nvrtc
    curl -fsSL -o cuda_nvrtc-linux-x86_64-12.6.20-archive.tar.xz "https://developer.download.nvidia.com/compute/cuda/redist/cuda_nvrtc/linux-x86_64/cuda_nvrtc-linux-x86_64-12.6.20-archive.tar.xz"
    tar -xJf "cuda_nvrtc-linux-x86_64-12.6.20-archive.tar.xz" -C ./nvrtc --strip-components=2
    cd nvrtc
    chmod 755 libnvrtc*
    find . -maxdepth 1 -type f -name "*libnvrtc.so.*" -exec sh -c 'ln -snf $(basename {}) libnvrtc.so' \;
    mkdir -p /usr/local/nvidia/lib
    mv -f libnvrtc* /usr/local/nvidia/lib
    rm -rf /tmp/*

    echo "/usr/local/nvidia/lib" >> /etc/ld.so.conf.d/nvidia.conf
    echo "/usr/local/nvidia/lib64" >> /etc/ld.so.conf.d/nvidia.conf

    # Cleanup
    apt-get remove -y --purge xz-utils curl
    rm -rf /var/lib/apt/lists/*
_ADD_NVRTC

LABEL org.opencontainers.image.source="https://github.com/games-on-whales/wolf/"
LABEL org.opencontainers.image.description="A base image with all the required GPU drivers"
