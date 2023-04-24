ARG BASE_IMAGE=ubuntu:22.10
FROM $BASE_IMAGE AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV BUILD_ARCHITECTURE=amd64
ENV DEB_BUILD_OPTIONS=noddebs

ARG SOURCE_PATH=/sources
ENV SOURCE_PATH=$SOURCE_PATH

ENV INCDIR=/usr/include
ENV LIBDIR=/usr/lib/x86_64-linux-gnu
ENV BINDIR=/usr/bin
ENV PKG_CONFIG_PATH=${LIBDIR}/pkgconfig:${SOURCE_PATH}/lib/pkgconfig
RUN mkdir -p ${INCDIR} ${PKG_CONFIG_PATH}

ARG DEV_PACKAGES=" \
    apt-transport-https curl wget git \
    build-essential gcc ninja-build pkg-config \
    debhelper gnupg devscripts mmv equivs \
    nasm subversion dh-autoreconf \
    libpciaccess-dev libwayland-dev libzstd-dev \
    python3-pip \
    zlib1g-dev llvm-15-dev libudev-dev libelf-dev libexpat1-dev"

WORKDIR $SOURCE_PATH

# Take a deep breath...
# We have to do this all in one go because we don't want the dev packages to be in the final build
ENV CCACHE_DIR=/cache/ccache
RUN <<_DEV_INSTALL
    #!/bin/bash
    set -e

    apt-get update -y
    apt-get install -y --no-install-recommends $DEV_PACKAGES

    # Manually install latest ninja and meson
    python3 -m pip install ninja meson cmake mako

    # LIBDRM
    cd ${SOURCE_PATH}
    mkdir libdrm
    cd libdrm
    libdrm_ver="2.4.115"
    libdrm_link="https://dri.freedesktop.org/libdrm/libdrm-${libdrm_ver}.tar.xz"
    wget ${libdrm_link} -O libdrm.tar.xz
    tar xaf libdrm.tar.xz
    meson setup libdrm-${libdrm_ver} drm_build \
        --prefix=/usr/ \
        --buildtype=release \
        -Dudev=false -Dtests=false -Dinstall-test-programs=false \
        -Damdgpu=enabled -Dradeon=enabled -Dintel=enabled \
        -Dvalgrind=disabled -Dfreedreno=disabled -Dvc4=disabled -Dvmwgfx=disabled -Dnouveau=disabled -Dman-pages=disabled
    meson configure drm_build
    ninja -C drm_build install

    # Install AMF
    cd ${SOURCE_PATH}
    git clone --depth=1 https://github.com/GPUOpen-LibrariesAndSDKs/AMF.git
    cd AMF/amf/public/include
    install -d include ${INCDIR}/AMF

    # LIBVA
    cd ${SOURCE_PATH}
    git clone -b 2.18.0 --depth=1 https://github.com/intel/libva.git
    cd libva
    ./autogen.sh
    ./configure --prefix=/usr/ \
        --enable-drm \
        --disable-glx --disable-x11 --disable-docs
    make -j$(nproc) && make install

    # LIBVA-UTILS
    cd ${SOURCE_DIR}
    git clone -b 2.18.0 --depth=1 https://github.com/intel/libva-utils.git
    cd libva-utils
    ./autogen.sh
    ./configure --prefix=/usr/
    make -j$(nproc) && make install
    rm -r /root/libva-utils/

    # Install Intel VAAPI Driver
    cd ${SOURCE_PATH}
    git clone --depth=1 https://github.com/intel/intel-vaapi-driver.git
    cd intel-vaapi-driver
    ./autogen.sh
    ./configure --prefix=/usr/
    make -j$(nproc) && make install

    # GMMLIB
    cd ${SOURCE_PATH}
    git clone -b intel-gmmlib-22.3.5 --depth=1 https://github.com/intel/gmmlib.git
    cd gmmlib
    mkdir build && cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr/ ..
    make -j$(nproc) && make install
    cd ${SOURCE_PATH}

    # NOTE: MFX Is needed in order to use QuickSynk
    # ONEVPL-INTEL-GPU (RT only)
    # Provides VPL runtime (libmfx-gen.so.1.2) for 11th Gen Tiger Lake and newer
    # Both MSDK and VPL runtime can be loaded by MFX dispatcher (libmfx.so.1)
    cd ${SOURCE_DIR}
    git clone -b intel-onevpl-23.1.5 --depth=1 https://github.com/oneapi-src/oneVPL-intel-gpu.git
    cd oneVPL-intel-gpu
    mkdir build && cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr/ \
          -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_RUNTIME=ON \
          -DBUILD_{TESTS,TOOLS}=OFF \
          -DMFX_ENABLE_{KERNELS,ENCTOOLS,AENC}=ON \
          ..
    make -j$(nproc) && make install
    rm -r /root/oneVPL-intel-gpu
    # MediaSDK (RT only)
    # Provides MSDK runtime (libmfxhw64.so.1) for 11th Gen Rocket Lake and older
    cd ${SOURCE_DIR}
    git clone -b intel-mediasdk-23.1.6 --depth=1 https://github.com/Intel-Media-SDK/MediaSDK.git
    cd MediaSDK
    mkdir build && cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr/ \
          -DBUILD_RUNTIME=ON \
          -DBUILD_SAMPLES=OFF -DBUILD_TUTORIALS=OFF -DBUILD_OPENCL=OFF \
          ..
    make -j$(nproc) && make install
    rm -r /root/MediaSDK


    # MEDIA-DRIVER
    # Full Feature Build: ENABLE_KERNELS=ON(Default) ENABLE_NONFREE_KERNELS=ON(Default)
    # Free Kernel Build: ENABLE_KERNELS=ON ENABLE_NONFREE_KERNELS=OFF
    cd ${SOURCE_PATH}
    git clone -b intel-media-23.1.6 --depth=1 https://github.com/intel/media-driver.git
    cd media-driver
    mkdir build && cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr/ \
          -DENABLE_KERNELS=ON \
          -DENABLE_NONFREE_KERNELS=ON \
          ..
    make -j$(nproc) && make install

    # libwayland is needed by vainfo, mesa (and later on by Gstreamer)
    apt-get install -y libwayland-client0 libwayland-server0 wayland-protocols


    # MESA
    # Minimal libs for AMD VAAPI
    cd ${SOURCE_PATH}
    git clone -b main https://gitlab.freedesktop.org/mesa/mesa.git
    cd mesa
    git checkout mesa-23.0.3
    cd ${SOURCE_PATH}
    # disable the broken hevc packed header
    MESA_VA_PIC="mesa/src/gallium/frontends/va/picture.c"
    MESA_VA_CONF="mesa/src/gallium/frontends/va/config.c"
    sed -i 's|handleVAEncPackedHeaderParameterBufferType(context, buf);||g' ${MESA_VA_PIC}
    sed -i 's|handleVAEncPackedHeaderDataBufferType(context, buf);||g' ${MESA_VA_PIC}
    sed -i 's|if (u_reduce_video_profile(ProfileToPipe(profile)) == PIPE_VIDEO_FORMAT_HEVC)|if (0)|g' ${MESA_VA_CONF}
    # force reporting all packed headers are supported
    sed -i 's|value = VA_ENC_PACKED_HEADER_NONE;|value = 0x0000001f;|g' ${MESA_VA_CONF}
    sed -i 's|if (attrib_list\[i\].type == VAConfigAttribEncPackedHeaders)|if (0)|g' ${MESA_VA_CONF}
    meson setup mesa mesa_build \
        --prefix=/usr/ \
        --buildtype=release \
        --wrap-mode=nofallback \
        -Db_ndebug=true \
        -Db_lto=false \
        -Dplatforms=wayland \
        -Dgallium-drivers=radeonsi \
        -Dvulkan-drivers=[] \
        -Ddri3=enabled \
        -Degl=disabled \
        -Dgallium-extra-hud=false -Dgallium-nine=false \
        -Dgallium-omx=disabled -Dgallium-vdpau=disabled -Dgallium-xa=disabled -Dgallium-opencl=disabled \
        -Dgallium-va=enabled \
        -Dvideo-codecs=h264dec,h264enc,h265dec,h265enc \
        -Dgbm=disabled \
        -Dgles1=disabled \
        -Dgles2=disabled \
        -Dopengl=false \
        -Dglvnd=false \
        -Dglx=disabled \
        -Dlibunwind=disabled \
        -Dllvm=enabled \
        -Dlmsensors=disabled \
        -Dosmesa=false \
        -Dshared-glapi=disabled \
        -Dvalgrind=disabled \
        -Dtools=[] \
        -Dzstd=enabled \
        -Dmicrosoft-clc=disabled
    meson configure mesa_build
    ninja -C mesa_build install

    # Final cleanup stage
    apt-get remove -y --purge $DEV_PACKAGES
    apt-get autoremove -y
    # We can now safely delete the repo + builds
    rm -rf  \
    $SOURCE_PATH \
    /var/lib/apt/lists/*
_DEV_INSTALL


ENV PKG_CONFIG_PATH=${LIBDIR}/pkgconfig

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

