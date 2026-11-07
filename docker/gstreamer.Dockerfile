ARG BASE_IMAGE=ghcr.io/games-on-whales/gpu-drivers:2025.05
FROM $BASE_IMAGE AS builder

# gst-wayland-display's patches/ chain must be applied to the GStreamer tree before
# building: it adds the vulkanh265enc / vulkanav1enc elements and the multi-session
# fixes the Vulkan encode path needs.
ARG GSTREAMER_VERSION=1.28.4
ARG GST_WAYLAND_COMMIT=0121934
ARG GST_INTERPIPE_REV=0c454917cb3cf4f2b2f2729f5f72a4dee072c7c9
# Vulkan headers >= 1.4.317 are needed for the vulkan-video encode plugin; with older
# ones the encoders are silently compiled out (GST_VULKAN_HAVE_VIDEO_EXTENSIONS stays
# unset). The distro package is not new enough on every base, so overlay a pinned set.
ARG VK_HEADERS_TAG=v1.4.341
ENV GSTREAMER_VERSION=$GSTREAMER_VERSION

ENV SOURCE_PATH=/sources/
WORKDIR $SOURCE_PATH

RUN <<_GSTREAMER_INSTALL
    #!/bin/bash
    set -e

    # x265-devel comes from RPM Fusion free, enabled by the gpu-drivers base image
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

    # Build gstreamer with the games-on-whales patch chain
    git clone https://gitlab.freedesktop.org/gstreamer/gstreamer.git $SOURCE_PATH/gstreamer
    cd ${SOURCE_PATH}/gstreamer
    git checkout $GSTREAMER_VERSION
    git submodule update --recursive --remote

    git clone https://github.com/games-on-whales/gst-wayland-display.git $SOURCE_PATH/gst-wayland-display
    git -C $SOURCE_PATH/gst-wayland-display checkout $GST_WAYLAND_COMMIT
    # Apply the patch chain in the order recorded in patches/series (single source of
    # truth, kept in step with gst-wayland-display's own CI and devcontainer).
    for p in $(grep -v -e '^#' -e '^[[:space:]]*$' $SOURCE_PATH/gst-wayland-display/patches/series); do
        echo "applying $p"
        git apply $SOURCE_PATH/gst-wayland-display/patches/$p || exit 1
    done

    # Vulkan headers overlay (>= 1.4.317) plus a bumped vulkan.pc so meson's vulkan_dep
    # probe enables GST_VULKAN_HAVE_VIDEO_EXTENSIONS (else the Vulkan encoders are
    # silently absent).
    git clone --depth 1 --branch ${VK_HEADERS_TAG} \
        https://github.com/KhronosGroup/Vulkan-Headers /opt/vkhdr
    mkdir -p /opt/vkpc && { \
        echo 'prefix=/opt/vkhdr'; \
        echo 'includedir=${prefix}/include'; \
        echo ''; \
        echo 'Name: Vulkan'; \
        echo 'Description: Vulkan headers overlay (video extensions)'; \
        echo 'Version: 1.4.341'; \
        echo 'Libs: -lvulkan'; \
        echo 'Cflags: -I${includedir}'; \
    } > /opt/vkpc/vulkan.pc

    # waylanddisplaysrc needs libwayland >= 1.23 (wl_client_set_max_buffer_size)
    pkg-config --atleast-version=1.23 wayland-server || {
        echo "ERROR: wayland-server >= 1.23 is required, found $(pkg-config --modversion wayland-server)" >&2
        exit 1
    }

    # see the list of possible options here: https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/main/meson_options.txt
    PKG_CONFIG_PATH=/opt/vkpc meson setup \
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
        -Dgst-plugins-bad:vulkan=enabled \
        -Dgst-plugins-bad:vulkan-video=enabled \
        -Dgst-plugins-base:gl=enabled \
        -Dgst-plugins-base:gl_winsys=wayland,egl,gbm,surfaceless \
        build

    # Hard check: without GST_VULKAN_HAVE_VIDEO_EXTENSIONS the Vulkan video encoders
    # (and the patched vulkanh265enc/vulkanav1enc) are silently compiled out.
    grep -q 'GST_VULKAN_HAVE_VIDEO_EXTENSIONS 1' \
        build/subprojects/gst-plugins-bad/gst-libs/gst/vulkan/gstvkconfig.h || {
            echo 'ERROR: Vulkan video extensions not enabled (Vulkan headers >= 1.4.317 needed)' >&2
            exit 1
        }

    meson compile -C build
    meson install -C build

    # Add GstInterpipe (games-on-whales fork; the pinned revision forwards context
    # queries across the interpipe boundary, which is how the Vulkan encoders reach
    # the source's GstVulkanDevice in Wolf's split producer/consumer pipelines)
    git init $SOURCE_PATH/gst-interpipe
    cd $SOURCE_PATH/gst-interpipe
    git fetch --depth 1 https://github.com/games-on-whales/gst-interpipe.git $GST_INTERPIPE_REV
    git checkout FETCH_HEAD
    mkdir build
    meson build -Denable-gtk-doc=false
    meson install -C build

    # Final cleanup stage
    dnf clean all
    rm -rf $SOURCE_PATH /opt/vkhdr

    # Our custom GStreamer lives in /usr/local/lib64 (Fedora libdir), which is
    # not on the runtime loader path by default. Register it so gst-inspect
    # and everything built on top of this image resolves OUR gstreamer
    # instead of the distro one in /lib64.
    echo "/usr/local/lib64" > /etc/ld.so.conf.d/00-gst-local.conf
    ldconfig
_GSTREAMER_INSTALL

LABEL org.opencontainers.image.source="https://github.com/games-on-whales/wolf/"
LABEL org.opencontainers.image.description="GStreamer: https://gstreamer.freedesktop.org/ (Fedora)"

ENTRYPOINT []
CMD ["/usr/local/bin/gst-inspect-1.0"]
