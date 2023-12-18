{ pkgs, ... }:
pkgs.stdenv.mkDerivation {
  pname = "gstreamer-wolf";
  version = "1.0";

  src = pkgs.fetchFromGitLab {
    owner = "gstreamer";
    repo = "gstreamer";
    rev = "4d13eddc8b6d3f42ba44682ba42048acf170547f";
    hash = "sha256-cXhJgmckfuennZeJgi4dNgqCXXM2OwXiml0CQfVmQw8=";
  };
  # src = let
  # pname = "gstreamer";
  # version = "1.22.7";
  # in pkgs.fetchurl
  # {
  # url = "https://gstreamer.freedesktop.org/src/${pname}/${pname}-${version}.tar.xz";
  # hash = "sha256-AeQsY1Kga9+kRW5ksGq32YxcSHolVXx2FVRjHL2mQhc=";
  # };
  depsBuildBuild = with pkgs; [ pkg-config ];
  strictDeps = true;
  nativeBuildInputs = with pkgs; [
    meson
    ninja
    # cmake
    pkg-config
    gettext
    bison
    flex
    python3
    makeWrapper
    glib
    wayland
    wayland-protocols
    libopus
    bash-completion
    # boost175
    libcap
    gobject-introspection
    nasm # needed
    qt6.qtbase
    qt6.qttools
    # qt6.wrapQtAppsHook
  ];
  postPatch = ''
    patchShebangs \
      subprojects/gstreamer/gst/parse/get_flex_version.py \
      gst/parse/gen_grammar.py.in \
      gst/parse/gen_lex.py.in \
      libs/gst/helpers/ptp_helper_post_install.sh \
      subprojects/gstreamer/scripts/extract-release-date-from-doap-file.py \
      subprojects/gst-plugins-base/scripts/meson-pkg-config-file-fixup.py \
      subprojects/gst-plugins-base/scripts/extract-release-date-from-doap-file.py \
      subprojects/gst-plugins-good/scripts/extract-release-date-from-doap-file.py
  '';

  dontWrapQtApps = true;
  preConfigure = ''
    ls -la
  '';

  buildInputs = with pkgs; [
    # flex
    # bison
    # python3
    # bash-completion
    # wayland
    # wayland-protocols
    # libcap
    # zxing # is needed
    # zbar # is needed
    # libraspberrypi
    libdrm
    libgudev
    # libva
    libtheora
    alsa-lib
    cdparanoia
    # libintl
    # libcap
    graphene
    libunwind
    elfutils
    gmp
    gsl
    # gobject-introspection
    # xorg.libXext
    xorg.libX11
    xorg.libXi
    xorg.libXv
    # xorg.libXfixes
    xorg.libXdamage
    aalib
    libxml2
    flac
    gdk-pixbuf
    gtk3
    libjack2
    lame
    twolame
    libcaca
    libdv
    mpg123
    libraw1394
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtwayland
    libshout
    taglib
    libvpx
    wavpack

    # #test
    # lcms
    # cairo
    # x264
    # x265
    # libaom
    # libwebp
    # resvg
    libGL
    libvisual
    libv4l
    libpulseaudio
    libavc1394
    libiec61883
    isocodes
    libpng
    libjpeg
    tremor
    pango
  ];
  propagatedBuildInputs = [ pkgs.glib ];

  mesonFlags = [
    "--buildtype=release"
    "--strip"
    "-Dgst-full-libraries=app,video"
    "-Dorc=disabled"
    "-Dgpl=enabled"
    "-Dbase=enabled"
    "-Dgood=enabled"
    "-Dugly=enabled"
    "-Drs=disabled"
    "-Dtls=disabled"
    "-Dgst-examples=disabled"
    "-Dlibav=disabled"
    "-Dtests=disabled"
    "-Dexamples=disabled"
    "-Ddoc=disabled"
    "-Dpython=disabled"
    "-Drtsp_server=disabled"
    "-Dqt5=disabled"
    "-Dbad=enabled"
    "-Dgst-plugins-good:soup=disabled"
    "-Dgst-plugins-good:ximagesrc=enabled"
    "-Dgst-plugins-good:pulse=enabled"
    "-Dgst-plugins-bad:x265=enabled"
    "-Dgst-plugins-bad:qsv=enabled"
    "-Dgst-plugins-bad:aom=enabled"
    "-Dgst-plugin-bad:nvcodec=enabled"
    "-Dvaapi=enabled"
    "-Dgstreamer:dbghelp=disabled" # not needed as we already provide libunwind and libdw, and dbghelp is a fallback to those
    "-Dgst-plugins-good:rpicamsrc=disabled"
    # "-Dgst-plugins-good:rpi-lib-dir=${pkgs.libraspberrypi}/lib"
    # "-Dgst-plugins-good:rpi-header-dir=${pkgs.libraspberrypi}/lib"
  ];

  # ]++ (if raspiCameraSupport then [
  # "-Drpi-lib-dir=${libraspberrypi}/lib"
  # ] else [
  # "-Drpicamsrc=disabled"
  # ]);
  # sourceRoot = ".";
  # postPatch = ''
  # export HOME=$(mktemp -d)
  # ln -s ${./Cargo.lock} Cargo.lock
  # '';
  # # buildPhase = "${pkgs.rust.envVars.setEnv} cargo cbuild --release --frozen --prefix=${placeholder "out"} --target ${pkgs.stdenv.hostPlatform.rust.rustcTarget}";
  # buildPhase = ''
  # export HOME=$(mktemp -d)
  # runHook preBuild
  # ${pkgs.rust.envVars.setEnv} cargo cbuild --release --frozen --prefix=${
  # placeholder "out"
  # } --target ${pkgs.stdenv.hostPlatform.rust.rustcTarget}
  # runHook postBuild
  # '';
  # # installPhase = "${pkgs.rust.envVars.setEnv} cargo cinstall --release --frozen --prefix=${placeholder "out"} --target ${pkgs.stdenv.hostPlatform.rust.rustcTarget}";
  # installPhase = ''
  # runHook preInstall
  # ${pkgs.rust.envVars.setEnv} cargo cinstall --release --frozen --prefix=${
  # placeholder "out"
  # } --target ${pkgs.stdenv.hostPlatform.rust.rustcTarget}
  # runHook postInstall
  # '';
}
