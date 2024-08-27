ARG BASE_IMAGE=ghcr.io/games-on-whales/gstreamer:1.24.6
########################################################
FROM $BASE_IMAGE AS wolf-builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    curl \
    ca-certificates \
    ninja-build \
    cmake \
    pkg-config \
    ccache \
    git \
    clang \
    libboost-thread-dev libboost-locale-dev libboost-filesystem-dev libboost-log-dev libboost-stacktrace-dev libboost-container-dev \
    libwayland-dev libwayland-server0 libinput-dev libxkbcommon-dev libgbm-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libevdev-dev \
    libpulse-dev \
    libunwind-dev \
    libudev-dev \
    libdrm-dev \
    libpci-dev \
    && rm -rf /var/lib/apt/lists/*

## Install Rust in order to build our custom compositor
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="$HOME/.cargo/bin:${PATH}"

WORKDIR /tmp/
RUN <<_GST_WAYLAND_DISPLAY
    #!/bin/bash
    set -e

    git clone https://github.com/games-on-whales/gst-wayland-display
    cd gst-wayland-display
    git checkout 48382dc
    cargo install cargo-c
    cargo cinstall -p c-bindings --prefix=/usr/local --libdir=/usr/local/lib/
_GST_WAYLAND_DISPLAY

## Install msgpack-c
RUN <<_MSGPACK
    #!/bin/bash
    set -e

    git clone https://github.com/msgpack/msgpack-c.git
    cd msgpack-c
    git checkout c-6.1.0
    mkdir build
    cd build

    cmake -DCMAKE_BUILD_TYPE=Release -DMSGPACK_BUILD_EXAMPLES=OFF -G Ninja ..
    cmake --build . --target install --config Release
_MSGPACK

COPY . /wolf/
WORKDIR /wolf

ENV CCACHE_DIR=/cache/ccache
ENV CMAKE_BUILD_DIR=/cache/cmake-build
RUN --mount=type=cache,target=/cache/ccache \
    cmake -B$CMAKE_BUILD_DIR \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_CXX_EXTENSIONS=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DBoost_USE_STATIC_LIBS=ON \
    -DBUILD_FAKE_UDEV_CLI=ON \
    -DBUILD_TESTING=OFF \
    -G Ninja && \
    ninja -C $CMAKE_BUILD_DIR wolf && \
    ninja -C $CMAKE_BUILD_DIR fake-udev && \
    # We have to copy out the built executables because this will only be available inside the buildkit cache
    cp $CMAKE_BUILD_DIR/src/moonlight-server/wolf /wolf/wolf && \
    cp $CMAKE_BUILD_DIR/src/fake-udev/fake-udev /wolf/fake-udev

########################################################
FROM $BASE_IMAGE AS runner
ENV DEBIAN_FRONTEND=noninteractive

# Wolf runtime dependencies
RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    ca-certificates \
    libssl3 \
    libevdev2 \
    libudev1 \
    libcurl4 \
    libdrm2 \
    libpci3 \
    libunwind8 \
    && rm -rf /var/lib/apt/lists/*

# gst-plugin-wayland runtime dependencies
RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    libwayland-server0 libinput10 libxkbcommon0 libgbm1 \
    libglvnd0 libgl1 libglx0 libegl1 libgles2 xwayland \
    && rm -rf /var/lib/apt/lists/*

ENV GST_PLUGIN_PATH=/usr/local/lib/x86_64-linux-gnu/gstreamer-1.0/
# Copying out our custom compositor from the build stage
COPY --from=wolf-builder /usr/local/lib/x86_64-linux-gnu/gstreamer-1.0/* $GST_PLUGIN_PATH
COPY --from=wolf-builder /usr/local/lib/liblibgstwaylanddisplay* /usr/local/lib/

WORKDIR /wolf

ARG WOLF_CFG_FOLDER=/etc/wolf/cfg
ENV WOLF_CFG_FOLDER=$WOLF_CFG_FOLDER
RUN mkdir -p $WOLF_CFG_FOLDER

COPY --from=wolf-builder /wolf/wolf /wolf/wolf
COPY --from=wolf-builder /wolf/fake-udev /wolf/fake-udev

ENV XDG_RUNTIME_DIR=/tmp/sockets \
    WOLF_LOG_LEVEL=INFO \
    WOLF_CFG_FILE=$WOLF_CFG_FOLDER/config.toml \
    WOLF_PRIVATE_KEY_FILE=$WOLF_CFG_FOLDER/key.pem \
    WOLF_PRIVATE_CERT_FILE=$WOLF_CFG_FOLDER/cert.pem \
    WOLF_PULSE_IMAGE=ghcr.io/games-on-whales/pulseaudio:master \
    WOLF_RENDER_NODE=/dev/dri/renderD128 \
    WOLF_STOP_CONTAINER_ON_EXIT=TRUE \
    WOLF_DOCKER_SOCKET=/var/run/docker.sock \
    RUST_BACKTRACE=full \
    RUST_LOG=WARN \
    HOST_APPS_STATE_FOLDER=/etc/wolf \
    GST_DEBUG=2 \
    PUID=0 \
    PGID=0 \
    UNAME="root"

# HTTPS
EXPOSE 47984/tcp
# HTTP
EXPOSE 47989/tcp
# Control
EXPOSE 47999/udp
# RTSP
EXPOSE 48010/tcp
# Video (up to 10 users)
EXPOSE 48100-48110/udp
# Audio (up to 10 users)
EXPOSE 48200-48210/udp

LABEL org.opencontainers.image.source="https://github.com/games-on-whales/wolf/"
LABEL org.opencontainers.image.description="Wolf: stream virtual desktops and games in Docker"

# See GOW/base-app
COPY --chmod=777 docker/startup.sh /opt/gow/startup-app.sh
ENTRYPOINT ["/entrypoint.sh"]
