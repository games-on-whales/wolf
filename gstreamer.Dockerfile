ARG BASE_IMAGE=ghcr.io/games-on-whales/gpu-drivers:2023.04
FROM $BASE_IMAGE AS builder
ENV DEBIAN_FRONTEND=noninteractive

ARG GSTREAMER_VERSION=1.22.0
ENV GSTREAMER_VERSION=$GSTREAMER_VERSION

ARG GST_SOURCE_PATH=/gstreamer/
ENV GST_SOURCE_PATH=$GST_SOURCE_PATH

ARG DEV_PACKAGES=" \
    ca-certificates \
    git \
    build-essential \
    libllvm15 \
    gcc \
    ccache \
    bison \
    python3 python3-pip \
    flex libx265-dev libopus-dev nasm libzxingcore-dev libzbar-dev \
    libx11-dev libxfixes-dev libxdamage-dev libwayland-dev libpulse-dev \
    "

WORKDIR $GST_SOURCE_PATH

# Take a deep breath...
# We have to do this all in one go because we don't want the dev packages to be in the final build
# The idea is that after `meson install` we should have everything in the right place and ready to be used.
RUN <<_GSTREAMER_INSTALL
    #!/bin/bash
    set -e

    apt-get update -y
    apt-get install -y --no-install-recommends $DEV_PACKAGES

    # Manually install latest ninja and meson
    python3 -m pip install ninja meson cmake

    # Build gstreamer
    cd ${GST_SOURCE_PATH}
    git clone https://gitlab.freedesktop.org/gstreamer/gstreamer.git $GST_SOURCE_PATH
    git checkout $GSTREAMER_VERSION
    # see the list of possible options here: https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/main/meson_options.txt \
    meson setup \
        --buildtype=release \
        --strip \
        -Dgst-full-libraries=app,video \
        -Dgpl=enabled  \
        -Dbase=enabled \
        -Dgood=enabled  \
        -Dugly=enabled \
        -Drs=disabled \
        -Dtls=disabled \
        -Dgst-examples=disabled \
        -Dlibav=disabled \
        -Dtests=disabled \
        -Dexamples=disabled \
        -Ddoc=disabled \
        -Dpython=disabled \
        -Drtsp_server=disabled \
        -Dqt5=disabled \
        -Dbad=enabled \
        -Dgst-plugins-good:soup=disabled \
        -Dgst-plugins-good:ximagesrc=enabled \
        -Dgst-plugins-good:pulse=enabled \
        -Dgst-plugins-bad:x265=enabled  \
        -Dgst-plugins-bad:qsv=enabled \
        -Dgst-plugin-bad:nvcodec=enabled  \
        -Dgst-plugin-bad:amfcodec=enabled \
        -Dvaapi=enabled \
        build
    meson compile -C build
    meson install -C build

    # Final cleanup stage
    apt-get remove -y --purge $DEV_PACKAGES
    # For some reason meson install wouldn't put libglib2.0 but we need it in order to compile Wolf
    apt-get install -y --no-install-recommends libglib2.0-dev
    # We can now safely delete the gstreamer repo + build folder
    rm -rf  \
    $GST_SOURCE_PATH \
    /var/lib/apt/lists/*
_GSTREAMER_INSTALL

CMD ["/usr/local/bin/gst-inspect-1.0"]
