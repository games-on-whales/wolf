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

# Adding missing libnvrtc.so for Nvidia
RUN --mount=type=cache,target=/var/cache/apt \
    apt-get update -y && \
    apt-get install -y wget gnupg2 && \
    apt-key del 7fa2af80 && \
    wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.0-1_all.deb && \
    dpkg -i cuda-keyring_1.0-1_all.deb && \
    rm cuda-keyring_1.0-1_all.deb && \
    apt-get update -y && \
    apt-get install -y cuda-nvrtc-dev-12-0 && \
    echo "/usr/local/cuda-12.0/targets/x86_64-linux/lib/" > /etc/ld.so.conf.d/cuda.conf && \
    apt-get remove --purge -y --autoremove wget gnupg2 && \
    rm -rf /var/lib/apt/lists/*

LABEL org.opencontainers.image.source="https://github.com/games-on-whales/wolf/"
LABEL org.opencontainers.image.description="A base image with all the required GPU drivers"
