# wolf:vulkan
#
# Wolf built on top of the native-Vulkan GStreamer stack. Unlike docker/wolf.Dockerfile
# (which builds on the stock gstreamer image and clones+cinstalls the plugin itself),
# this variant is FROM ghcr.io/games-on-whales/gst-wayland-display:vulkan, which already
# carries patched GStreamer 1.28.4 (Vulkan video encode + the vulkanh264enc DPB-pool
# patch) under /opt/gst AND the gst-wayland-display plugin installed there. So we only
# compile Wolf on top — no gst build, no plugin clone. See that image's docker/vulkan.Dockerfile.
ARG BASE_IMAGE=ghcr.io/games-on-whales/gst-wayland-display:vulkan
########################################################
FROM $BASE_IMAGE AS wolf-builder

# Wolf's own build dependencies. The gst toolchain (gcc, cmake, ninja, clang, the
# gst -devel headers in /opt/gst) already comes from the base image; here we add the
# libraries Wolf links that the gst image doesn't carry.
RUN dnf install -y \
    ccache \
    boost-devel \
    libevdev-devel \
    pulseaudio-libs-devel \
    libcurl-devel \
    pciutils-devel \
    glibc-static \
    libstdc++-static \
    && dnf clean all

# gst (incl. the gst-wayland-display .pc/.a/.so) is under /opt/gst; the base image
# already exports PKG_CONFIG_PATH / GST_PLUGIN_PATH / LD_LIBRARY_PATH for it, so
# Wolf's CMake finds gstreamer-1.0 and the statically-linked gstwaylanddisplay there.

# Cache-bust: force a clean recompile on the freshly-published base
# (gst-wayland-display:vulkan carrying the producer P010+HDR + encoder HDR SEI +
# vulkanh265enc.patch). Bump the value to invalidate the build cache.
ARG WOLF_VULKAN_CACHEBUST=2026-06-29-hdr-9-dynamic
RUN echo "rebuild on fresh base: ${WOLF_VULKAN_CACHEBUST}"

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
    cp $CMAKE_BUILD_DIR/src/moonlight-server/wolf /wolf/wolf && \
    cp $CMAKE_BUILD_DIR/src/fake-udev/fake-udev /wolf/fake-udev

########################################################
FROM $BASE_IMAGE AS runner

# Wolf runtime dependencies (gst + the plugin + freeworld RADV mesa already in the base)
RUN dnf install -y \
    ca-certificates openssl-libs libicu libevdev systemd-libs libcurl libdrm \
    pciutils-libs libunwind \
    libwayland-server libinput libxkbcommon mesa-libgbm \
    libglvnd mesa-libGL mesa-libEGL mesa-libGLES xorg-x11-server-Xwayland hwdata \
    && dnf clean all

# Embedded PulseAudio (supervised by supervisord, see startup.sh + supervisord.conf):
# Wolf runs its own PA server so audio is up as soon as Wolf boots, no external
# sidecar. The Fedora base ships pipewire-pulseaudio (a PA-compat shim) which
# conflicts with the real daemon; --allowerasing swaps it out.
RUN dnf install -y --allowerasing \
    pulseaudio pulseaudio-utils supervisor \
    && dnf clean all

COPY docker/supervisord.conf /etc/supervisord.conf

# The plugin + gst live under /opt/gst in the base image (GST_PLUGIN_PATH and
# LD_LIBRARY_PATH are already exported there); register /opt/gst/lib64 with the
# dynamic linker so wolf and gst-inspect resolve the gstreamer .so's at runtime.
RUN echo /opt/gst/lib64 > /etc/ld.so.conf.d/gst-vulkan.conf && ldconfig

WORKDIR /wolf
ENV WOLF_CFG_FOLDER=/etc/wolf/cfg

# Disable the lavapipe (llvmpipe) Vulkan ICD process-wide. On mixed-GPU / software-fallback
# hosts the Vulkan loader otherwise dlopen()s lavapipe during vkCreateInstance (both the
# producer's modifier query AND the vulkanh26x encoder's gst.vulkan.instance), and its libLLVM
# static init collides with the libLLVM already loaded by the mesa GLES renderer -> LLVM
# "option registered more than once" -> abort/std::terminate. lavapipe can't do Vulkan video
# encode anyway. Set at the image level (not just the producer's plugin_init) so it covers the
# encoder's instance regardless of plugin load order. Honour an explicit override at run time.
ENV VK_LOADER_DRIVERS_DISABLE="*lvp_icd*"

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
    WOLF_DEFAULT_RUN_UID=1000 \
    WOLF_DEFAULT_RUN_GID=1000 \
    RUST_BACKTRACE=full \
    RUST_LOG=WARN \
    HOST_APPS_STATE_FOLDER=/etc/wolf \
    GST_DEBUG=2 \
    PUID=0 \
    PGID=0 \
    UNAME="root"

VOLUME /run/user/wolf/
ENV XDG_RUNTIME_DIR=/run/user/wolf

# HTTPS / HTTP / Control / RTSP / Video / Audio
EXPOSE 47984/tcp
EXPOSE 47989/tcp
EXPOSE 47999/udp
EXPOSE 48010/tcp
EXPOSE 48100/udp
EXPOSE 48200/udp

LABEL org.opencontainers.image.source="https://github.com/games-on-whales/wolf/"
LABEL org.opencontainers.image.description="Wolf with native Vulkan NV12 encode (Fedora + GStreamer 1.28.4 vulkan-video)"

COPY --chmod=777 docker/startup.sh /opt/gow/startup-app.sh
ENTRYPOINT ["/entrypoint.sh"]
