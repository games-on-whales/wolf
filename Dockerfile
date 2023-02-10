ARG GSTREAMER_VERSION=1.22.0
########################################################
FROM gameonwhales/gstreamer:$GSTREAMER_VERSION AS wolf-builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    ca-certificates \
    ninja-build \
    cmake \
    pkg-config \
    ccache \
    git \
    clang \
    libboost-thread-dev libboost-locale-dev libboost-filesystem-dev libboost-log-dev libboost-stacktrace-dev \
    libssl-dev \
    libevdev-dev \
    libunwind-dev \
    && rm -rf /var/lib/apt/lists/*

COPY src /wolf/src
COPY cmake /wolf/cmake
COPY CMakeLists.txt /wolf/CMakeLists.txt
WORKDIR /wolf

ENV CCACHE_DIR=/cache/ccache
ENV CMAKE_BUILD_DIR=/cache/cmake-build
RUN --mount=type=cache,target=/cache/ccache \
    cmake -B$CMAKE_BUILD_DIR \
    -DCMAKE_BUILD_TYPE=Release \
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
FROM rust:1.66-slim AS gst-plugin-wayland

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    git ca-certificates pkg-config \
    libwayland-dev libwayland-server0 libudev-dev libinput-dev libxkbcommon-dev libgbm-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    && rm -rf /var/lib/apt/lists/*

ARG SUNRISE_SHA=6506ad500c16e79a2f350fc6b39f0ee587eabe26
ENV SUNRISE_SHA=$SUNRISE_SHA
RUN git clone https://github.com/Drakulix/sunrise.git && \
    cd sunrise && \
    git checkout $SUNRISE_SHA

WORKDIR /sunrise/gst-plugin-wayland-display

RUN --mount=type=cache,target=/usr/local/cargo/registry cargo build --release


########################################################
FROM gameonwhales/gstreamer:$GSTREAMER_VERSION AS runner
ENV DEBIAN_FRONTEND=noninteractive

# Wolf runtime dependencies
# curl only used by plugin curlhttpsrc (remote video play)
RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    ca-certificates libcurl4 \
    tini \
    libssl3 \
    libevdev2 \
    va-driver-all intel-media-va-driver-non-free  \
    && rm -rf /var/lib/apt/lists/*

# gst-plugin-wayland runtime dependencies
RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    libwayland-server0 libinput10 libxkbcommon0 libgbm1 \
    libglvnd0 libgl1  libglx0 libegl1 libgles2 \
    && rm -rf /var/lib/apt/lists/*

# TODO: avoid running as root

ENV GST_PLUGIN_PATH=/usr/local/lib/x86_64-linux-gnu/gstreamer-1.0/
COPY --from=wolf-builder /wolf/wolf /wolf/wolf
COPY --from=gst-plugin-wayland /sunrise/gst-plugin-wayland-display/target/release/libgstwaylanddisplay.so $GST_PLUGIN_PATH/libgstwaylanddisplay.so

# Here is where the dinamically created wayland sockets will be stored
ENV XDG_RUNTIME_DIR=/wolf/run/
RUN mkdir $XDG_RUNTIME_DIR

WORKDIR /wolf

ARG WOLF_CFG_FOLDER=/wolf/cfg
ENV WOLF_CFG_FOLDER=$WOLF_CFG_FOLDER
RUN mkdir $WOLF_CFG_FOLDER

ENV LOG_LEVEL=INFO
ENV CFG_FILE=$WOLF_CFG_FOLDER/config.toml
ENV PRIVATE_KEY_FILE=$WOLF_CFG_FOLDER/key.pem
ENV PRIVATE_CERT_FILE=$WOLF_CFG_FOLDER/cert.pem
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

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["/wolf/wolf"]