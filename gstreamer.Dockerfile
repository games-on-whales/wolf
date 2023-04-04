FROM ubuntu:22.04 AS builder
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
    flex libx265-dev nasm libva-dev libzxingcore-dev libzbar-dev \
    libx11-dev libxfixes-dev libxdamage-dev libwayland-dev libpulse-dev \
    "

WORKDIR $GST_SOURCE_PATH

# Take a deep breath...
# We have to do this all in one go because we don't want the dev packages to be in the final build
# The idea is that after `meson install` we should have everything in the right place and ready to be used.
ENV PKG_CONFIG_PATH=/usr/local/lib/x86_64-linux-gnu/pkgconfig
ENV CCACHE_DIR=/cache/ccache
RUN --mount=type=cache,target=/cache/ccache \
    --mount=type=cache,target=/var/cache/apt \
    apt-get update -y && \
    apt-get install -y --no-install-recommends $DEV_PACKAGES && \
    # Manually install latest ninja and meson (Gstreamer requires versions that aren't yet in Ubuntu:22.04) \
    python3 -m pip install ninja meson && \
    # Build gstreamer \
    git clone https://gitlab.freedesktop.org/gstreamer/gstreamer.git $GST_SOURCE_PATH && \
    git checkout $GSTREAMER_VERSION && \
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
        -Dgst-plugins-good:ximagesrc=enabled \
        -Dgst-plugins-good:pulse=enabled \
        -Dgst-plugins-bad:x265=enabled  \
        -Dgst-plugin-bad:nvcodec=enabled  \
        -Dgst-plugin-bad:amfcodec=enabled \
        -Dvaapi=enabled \
        build && \
    meson compile -C build && \
    meson install -C build && \
    # Final cleanup stage \
    apt-get remove -y --purge $DEV_PACKAGES && \
    # For some reason meson install wouldn't put libglib2.0 but we need it in order to compile Wolf
    apt-get install -y --no-install-recommends libglib2.0-dev && \
    # We can now safely delete the gstreamer repo + build folder
    rm -rf  \
    $GST_SOURCE_PATH \
    /var/lib/apt/lists/*

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
    apt-get purge -y wget gnupg2 && \
    rm -rf /var/lib/apt/lists/*

CMD ["/usr/local/bin/gst-inspect-1.0"]
