ARG BASE_IMAGE=ghcr.io/games-on-whales/gstreamer:pr-396-fedora
########################################################
FROM $BASE_IMAGE AS wolf-builder

RUN dnf install -y \
    curl \
    ca-certificates \
    ninja-build \
    cmake \
    pkg-config \
    ccache \
    git \
    clang \
    gcc-c++ \
    glibc-static \
    libstdc++-static \
    boost-devel \
    wayland-devel libinput-devel libxkbcommon-devel mesa-libgbm-devel \
    libcurl-devel \
    openssl-devel \
    libevdev-devel \
    pulseaudio-libs-devel \
    libunwind-devel \
    systemd-devel \
    libdrm-devel \
    pciutils-devel \
    glib2-devel mesa-libEGL-devel mesa-libGLES-devel libglvnd-devel \
    && dnf clean all

## Install Rust in order to build our custom compositor
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="$HOME/.cargo/bin:${PATH}"

ARG RUST_VERSION=1.91.1
ENV RUST_VERSION=$RUST_VERSION
RUN rustup install $RUST_VERSION && rustup default $RUST_VERSION

WORKDIR /tmp/
RUN <<_GST_WAYLAND_DISPLAY
    #!/bin/bash
    set -e

    git clone https://github.com/games-on-whales/gst-wayland-display
    cd gst-wayland-display
    git checkout 67b1183
    # Pinned to 0.10.20: 0.10.21+ requires rustc 1.92, upgrade RUST_VERSION above if unpinning
    cargo install cargo-c@0.10.20 --locked
    cargo cinstall --features="cuda" --prefix=/usr/local/lib/x86_64-linux-gnu/ --libdir=/usr/local/lib/x86_64-linux-gnu/gstreamer-1.0
_GST_WAYLAND_DISPLAY

COPY . /wolf/
WORKDIR /wolf

ENV CCACHE_DIR=/cache/ccache
ENV CMAKE_BUILD_DIR=/cache/cmake-build
RUN --mount=type=cache,target=/cache/ccache \
    cmake -B$CMAKE_BUILD_DIR \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_CXX_EXTENSIONS=OFF \
    -DCMAKE_CXX_FLAGS="-Wno-missing-template-arg-list-after-template-kw" \
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

# Wolf runtime dependencies
RUN dnf install -y \
    ca-certificates \
    openssl-libs \
    libicu \
    libevdev \
    systemd-libs \
    libcurl \
    libdrm \
    pciutils-libs \
    libunwind \
    && dnf clean all

# gst-plugin-wayland runtime dependencies
RUN dnf install -y \
    libwayland-server libinput libxkbcommon mesa-libgbm \
    libglvnd mesa-libGL mesa-libEGL mesa-libGLES xorg-x11-server-Xwayland hwdata \
    && dnf clean all

ENV GST_PLUGIN_PATH=/usr/local/lib/x86_64-linux-gnu/gstreamer-1.0/
# Copying out our custom compositor from the build stage
COPY --from=wolf-builder /usr/local/lib/x86_64-linux-gnu/gstreamer-1.0/* $GST_PLUGIN_PATH
COPY --from=wolf-builder /usr/local/lib/liblibgstwaylanddisplay* /usr/local/lib/

# Bundle the exact libicu the builder linked wolf against, so a builder/runner
# layer-cache skew can't produce a wolf binary that can't resolve its own
# libicuuc soname at runtime.
COPY --from=wolf-builder /usr/lib64/libicu*.so.* /usr/lib64/

WORKDIR /wolf

ENV WOLF_CFG_FOLDER=/etc/wolf/cfg

COPY --from=wolf-builder /wolf/wolf /wolf/wolf
COPY --from=wolf-builder /wolf/fake-udev /wolf/fake-udev

ENV GST_GL_API=gles2 \
    GST_GL_PLATFORM=egl \
    GST_GL_WINDOW=surfaceless \
    WOLF_USE_ZERO_COPY=TRUE \
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

# Setting up XDG_RUNTIME_DIR this will automatically create a volume when starting the container
VOLUME /run/user/wolf/
ENV XDG_RUNTIME_DIR=/run/user/wolf

# HTTPS
EXPOSE 47984/tcp
# HTTP
EXPOSE 47989/tcp
# Control
EXPOSE 47999/udp
# RTSP
EXPOSE 48010/tcp
# Video
EXPOSE 48100/udp
# Audio
EXPOSE 48200/udp

LABEL org.opencontainers.image.source="https://github.com/games-on-whales/wolf/"
LABEL org.opencontainers.image.description="Wolf: stream virtual desktops and games in Docker (Fedora)"

# See GOW/base-app
COPY --chmod=777 docker/startup.sh /opt/gow/startup-app.sh
ENTRYPOINT ["/entrypoint.sh"]
