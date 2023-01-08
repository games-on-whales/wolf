########################################################
FROM ubuntu:22.04 AS wolf-builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    ca-certificates \
    ninja-build \
    cmake \
    ccache \
    git \
    clang \
    libboost-thread-dev libboost-filesystem-dev libboost-log-dev libboost-stacktrace-dev \
    libssl-dev \
    libgstreamer1.0-dev \
    libevdev-dev \
    libunwind-dev \
    && rm -rf /var/lib/apt/lists/*

COPY . /wolf
WORKDIR /wolf

ENV CCACHE_DIR=/cache/ccache
ENV CMAKE_BUILD_DIR=/cache/cmake-build
RUN --mount=type=cache,target=/cache/ccache \
    --mount=type=cache,target=/cache/cmake-build \
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
# TODO: build gstreamer plugin manually
########################################################
FROM ubuntu:22.04 AS runner
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get install -y --no-install-recommends \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly gstreamer1.0-plugins-bad \
    libssl3 \
    libevdev2 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=wolf-builder /wolf/wolf /wolf/wolf

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

CMD ["/wolf/wolf"]