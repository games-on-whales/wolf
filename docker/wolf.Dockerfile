ARG BASE_IMAGE=ghcr.io/games-on-whales/gstreamer:1.22.0
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
    libboost-thread-dev libboost-locale-dev libboost-filesystem-dev libboost-log-dev libboost-stacktrace-dev \
    libwayland-dev libwayland-server0 libinput-dev libxkbcommon-dev libgbm-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libevdev-dev \
    libpulse-dev \
    libunwind-dev \
    libudev-dev \
    && rm -rf /var/lib/apt/lists/*

## Install Rust in order to build our custom compositor (the build will be done inside Cmake)
RUN curl https://sh.rustup.rs -sSf | sh -s -- -y
ENV PATH="/root/.cargo/bin:${PATH}"

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
    -DBUILD_TESTING=OFF \
    -G Ninja && \
    ninja -C $CMAKE_BUILD_DIR && \
    # We have to copy out the built executable because this will only be available inside the buildkit cache
    cp $CMAKE_BUILD_DIR/src/wolf/wolf /wolf/wolf

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
    && rm -rf /var/lib/apt/lists/*

# gst-plugin-wayland runtime dependencies
RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    libwayland-server0 libinput10 libxkbcommon0 libgbm1 \
    libglvnd0 libgl1 libglx0 libegl1 libgles2 xwayland \
    && rm -rf /var/lib/apt/lists/*

ENV GST_PLUGIN_PATH=/usr/local/lib/x86_64-linux-gnu/gstreamer-1.0/
COPY --from=wolf-builder /wolf/wolf /wolf/wolf

WORKDIR /wolf

ARG WOLF_CFG_FOLDER=/wolf/cfg
ENV WOLF_CFG_FOLDER=$WOLF_CFG_FOLDER
RUN mkdir $WOLF_CFG_FOLDER

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
    NVIDIA_DRIVER_VOLUME_NAME=nvidia-driver-vol \
    HOST_APPS_STATE_FOLDER=/etc/wolf \
    GST_DEBUG=2 \
    PUID=0 \
    PGID=0 \
    UNAME="root"

VOLUME $WOLF_CFG_FOLDER

# HTTPS
EXPOSE 47984/tcp
# HTTP
EXPOSE 47989/tcp
# Video
EXPOSE 47998/udp
# Control
EXPOSE 47999/udp
# Audio
EXPOSE 48000/udp
# RTSP
EXPOSE 48010/tcp

# See GOW/base-app
COPY --chmod=777 docker/startup.sh /opt/gow/startup-app.sh
ENTRYPOINT ["/entrypoint.sh"]