ARG BASE_IMAGE=ghcr.io/games-on-whales/gpu-drivers:2025.05
FROM $BASE_IMAGE AS builder

ARG GSTREAMER_VERSION=1.26.7
ENV GSTREAMER_VERSION=$GSTREAMER_VERSION

ENV SOURCE_PATH=/sources/
WORKDIR $SOURCE_PATH

RUN <<_GSTREAMER_INSTALL
    #!/bin/bash
    set -e

    DEV_PACKAGES=" \
        gcc gcc-c++ ninja-build meson cmake ccache bison libatomic \
        ca-certificates git \
        flex x265-devel opus-devel nasm zxing-cpp-devel zbar-devel libdrm-devel libva-devel \
        libvpl-devel libunwind libcap \
        libX11-devel libxcb-devel libXfixes-devel libXdamage-devel wayland-devel wayland-protocols-devel pulseaudio-libs-devel glib2-devel \
        openjpeg2-devel lcms2-devel cairo-devel cairo-gobject-devel libwebp librsvg2-devel libaom-devel \
        harfbuzz-devel pango-devel libsoup-devel libglvnd-devel mesa-libgbm-devel mesa-libEGL-devel \
        mesa-libGLU-devel freeglut-devel mesa-libGL-devel mesa-libGLES-devel libgudev-devel
        "
    dnf install -y $DEV_PACKAGES

    # Build gstreamer
    git clone https://gitlab.freedesktop.org/gstreamer/gstreamer.git $SOURCE_PATH/gstreamer
    cd ${SOURCE_PATH}/gstreamer
    git checkout $GSTREAMER_VERSION
    git submodule update --recursive --remote
    # see the list of possible options here: https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/main/meson_options.txt
    meson setup \
        --buildtype=release \
        --strip \
        -Dgst-full-libraries=app,video \
        -Dorc=disabled \
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
        -Dgst-plugins-bad:aom=enabled \
        -Dgst-plugins-bad:nvcodec=enabled  \
        -Dgst-plugins-base:gl=enabled  \
        -Dgstreamer-vaapi:x11=disabled \
        -Dgst-plugins-base:gl_winsys=wayland,egl,gbm,surfaceless  \
        -Dvaapi=enabled \
        build
    meson compile -C build
    meson install -C build

    # Add GstInterpipe
    git clone https://github.com/games-on-whales/gst-interpipe.git $SOURCE_PATH/gst-interpipe
    cd $SOURCE_PATH/gst-interpipe
    mkdir build
    meson build -Denable-gtk-doc=false
    meson install -C build

    # Final cleanup stage
    dnf clean all
    rm -rf $SOURCE_PATH
_GSTREAMER_INSTALL

LABEL org.opencontainers.image.source="https://github.com/games-on-whales/wolf/"
LABEL org.opencontainers.image.description="GStreamer: https://gstreamer.freedesktop.org/ (Fedora)"

ENTRYPOINT []
CMD ["/usr/local/bin/gst-inspect-1.0"]
