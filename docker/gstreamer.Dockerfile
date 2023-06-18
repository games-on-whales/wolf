ARG BASE_IMAGE=ghcr.io/games-on-whales/gpu-drivers:2023.04
FROM $BASE_IMAGE AS builder
ENV DEBIAN_FRONTEND=noninteractive
ENV BUILD_ARCHITECTURE=amd64
ENV DEB_BUILD_OPTIONS=noddebs

ARG GSTREAMER_VERSION=1.22.0
ENV GSTREAMER_VERSION=$GSTREAMER_VERSION

ENV SOURCE_PATH=/sources/
WORKDIR $SOURCE_PATH
COPY --chmod=777 gstreamer.control $SOURCE_PATH/gstreamer.control

RUN <<_GSTREAMER_INSTALL
    #!/bin/bash
    set -e

    DEV_PACKAGES=" \
        build-essential ninja-build gcc meson cmake ccache bison equivs \
        ca-certificates git libllvm15 \
        flex libx265-dev libopus-dev nasm libzxingcore-dev libzbar-dev libdrm-dev libva-dev \
        libmfx-dev libvpl-dev libmfx-tools libunwind8 libcap2-bin liborc-0.4-dev \
        libx11-dev libxfixes-dev libxdamage-dev libwayland-dev libpulse-dev libglib2.0-dev \
        libopenjp2-7-dev liblcms2-dev libcairo2-dev libcairo-gobject2 libwebp7 librsvg2-dev
        "
    apt-get update -y
    apt-get install -y --no-install-recommends $DEV_PACKAGES

    # Build gstreamer
    git clone -b $GSTREAMER_VERSION --depth=1 https://gitlab.freedesktop.org/gstreamer/gstreamer.git $SOURCE_PATH/gstreamer
    cd ${SOURCE_PATH}/gstreamer
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
        -Dvaapi=enabled \
        build
    meson compile -C build
    meson install -C build

    # fake install, this way we'll keep runtime dependencies and we can safely delete all the additional packages
    equivs-build $SOURCE_PATH/gstreamer.control
    dpkg -i gstreamer-wolf_${GSTREAMER_VERSION}_all.deb

    # Final cleanup stage
    apt-mark auto $DEV_PACKAGES
    apt-get autoremove -y --purge
    # We can now safely delete the gstreamer repo + build folder
    rm -rf  \
    $SOURCE_PATH \
    /var/lib/apt/lists/*
_GSTREAMER_INSTALL

LABEL org.opencontainers.image.source="https://github.com/games-on-whales/wolf/"
LABEL org.opencontainers.image.description="GStreamer: https://gstreamer.freedesktop.org/"


ENTRYPOINT ["/bin/bash"]
CMD ["/usr/local/bin/gst-inspect-1.0"]
