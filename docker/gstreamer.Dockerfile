FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive


RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    ca-certificates \
    git \
    build-essential \
    meson bison ninja-build \
    flex libx265-dev nasm libva-dev libzxingcore-dev libzbar-dev \
    libx11-dev libxfixes-dev libxdamage-dev libwayland-dev libpulse-dev \
    && rm -rf /var/lib/apt/lists/*

######################################################
# Install nvidia cuda toolkit
# Adapted from: https://developer.nvidia.com/cuda-downloads?target_os=Linux&target_arch=x86_64&Distribution=Ubuntu&target_version=22.04&target_type=deb_local
#
# Since 1.17.1, the `nvcodec` plugin does not need access to the Nvidia Video SDK
# or the CUDA SDK. It now loads everything at runtime. Hence, it is now enabled
# by default on all platforms.

#WORKDIR /cuda
#
#RUN wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-ubuntu2204.pin && \
#    mv cuda-ubuntu2204.pin /etc/apt/preferences.d/cuda-repository-pin-600 && \
#    wget https://developer.download.nvidia.com/compute/cuda/12.0.0/local_installers/cuda-repo-ubuntu2204-12-0-local_12.0.0-525.60.13-1_amd64.deb && \
#    dpkg -i cuda-repo-ubuntu2204-12-0-local_12.0.0-525.60.13-1_amd64.deb && \
#    cp /var/cuda-repo-ubuntu2204-12-0-local/cuda-*-keyring.gpg /usr/share/keyrings/ && \
#    apt-get update && \
#    apt-get install -y cuda && \
#    rm cuda-repo-ubuntu2204-12-0-local_12.0.0-525.60.13-1_amd64.deb && \
#    rm -rf /var/lib/apt/lists/*

######################################################
# Build gstreamer plugins
# see the list of possible options here: https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/main/meson_options.txt

WORKDIR /gstreamer
RUN git clone https://gitlab.freedesktop.org/gstreamer/gstreamer.git

ARG GSTREAMER_VERSION=1.20.3
ENV GSTREAMER_VERSION=$GSTREAMER_VERSION
WORKDIR /gstreamer/gstreamer
RUN git checkout $GSTREAMER_VERSION

# vaapi can't be added to the static build
# Can't make a static build: GStreamer-VAAPI plugin not supported with `static` builds yet.

RUN meson setup \
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
    -Dgst-plugins-good:ximagesrc=enabled \
    -Dgst-plugins-good:pulse=enabled \
    -Dgst-plugins-bad:x265=enabled  \
    -Dgst-plugin-bad:nvcodec=enabled  \
    -Dvaapi=enabled \
    build

RUN ninja -C build

FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    libgstreamer1.0-0 \
    libcairo-gobject2 libgdk-pixbuf-2.0-0 libva2 libva-x11-2 libva-drm2 libxdamage1 liblcms2-2  \
    libopenexr25 libzbar0 libopenjp2-7 librsvg2-2 libx265-199 libzxingcore1 libopenh264-6 libpulse0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /gstreamer/gstreamer/build/subprojects/ /gstreamer/

ENV GST_PLUGIN_PATH=/gstreamer/
CMD ["/gstreamer/gstreamer/tools/gst-inspect-1.0"]
