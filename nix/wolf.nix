{ pkgs, self, deps, ... }:
let
  fake-udev = pkgs.stdenv.mkDerivation {
    pname = "fake-udev";
    version = "1.0";
    src = self;

    nativeBuildInputs = with pkgs; [ cmake pkg-config ninja autoPatchelfHook ];

    buildInputs = with pkgs; [
      glibc.static
      deps.gst-wayland-display
      wayland
      icu
      pciutils
      git
      range-v3
      elfutils
      libinput
      libxkbcommon
      pcre2
      libunwind
      orc
      libdrm
      boost175
      gst_all_1.gstreamer
      # Common plugins like "filesrc" to combine within e.g. gst-launch
      gst_all_1.gst-plugins-base
      # Specialized plugins separated by quality
      gst_all_1.gst-plugins-good
      gst_all_1.gst-plugins-bad
      gst_all_1.gst-plugins-ugly
      # Plugins to reuse ffmpeg to play almost every video format
      gst_all_1.gst-libav
      # Support the Video Audio (Hardware) Acceleration API
      gst_all_1.gst-vaapi
      libevdev
      libpulseaudio
      openssl
      curl
    ];

    cmakeFlags = [
      "-DFETCHCONTENT_SOURCE_DIR_IMMER=${deps.immer_src}"
      "-DFETCHCONTENT_SOURCE_DIR_EVENTBUS=${deps.eventbus_src}"
      "-DFETCHCONTENT_SOURCE_DIR_BOOST_JSON=${deps.boost-json_src}"
      "-DFETCHCONTENT_SOURCE_DIR_RANGE=${deps.range_src}"
      "-DFETCHCONTENT_SOURCE_DIR_FMTLIB=${deps.fmtlib_src}"
      "-DFETCHCONTENT_SOURCE_DIR_NANORS=${deps.nanors_src}"
      "-DFETCHCONTENT_SOURCE_DIR_PEGLIB=${deps.peglib_src}"
      "-DFETCHCONTENT_SOURCE_DIR_SIMPLEWEBSERVER=${deps.simplewebserver_src}"
      "-DFETCHCONTENT_SOURCE_DIR_TOML=${deps.toml_src}"
      "-DFETCHCONTENT_SOURCE_DIR_ENET=${deps.enet_src}"
      "-DFETCHCONTENT_SOURCE_DIR_CPPTRACE=${deps.cpptrace_src}"
      "-DFETCHCONTENT_SOURCE_DIR_LIBDWARF=${deps.libdwarf_src}"
      "-DCMAKE_BUILD_TYPE=Release"
      "-DCMAKE_CXX_STANDARD=17"
      "-DCMAKE_CXX_EXTENSIONS=OFF"
      "-DBUILD_SHARED_LIBS=OFF"
      "-DBUILD_FAKE_UDEV_CLI=ON"
      "-DBUILD_TESTING=OFF"
      "-G Ninja"
    ];
    buildPhase = ''
      ninja fake-udev
    '';
    installPhase = ''
      mkdir -p $out/bin
      cp ./src/fake-udev/fake-udev $out/bin/fake-udev
    '';
  };
in pkgs.stdenv.mkDerivation {
  pname = "wolf";
  version = "1.0";
  src = self;

  nativeBuildInputs = with pkgs; [ cmake pkg-config ninja wrapGAppsHook ];

  buildInputs = with pkgs; [
    fake-udev
    deps.gst-wayland-display
    wayland
    icu
    pciutils
    git
    range-v3
    elfutils
    libinput
    libxkbcommon
    pcre2
    libunwind
    orc
    libdrm
    boost175
    gst_all_1.gstreamer
    # Common plugins like "filesrc" to combine within e.g. gst-launch
    gst_all_1.gst-plugins-base
    # Specialized plugins separated by quality
    gst_all_1.gst-plugins-good
    gst_all_1.gst-plugins-bad
    gst_all_1.gst-plugins-ugly
    # Plugins to reuse ffmpeg to play almost every video format
    gst_all_1.gst-libav
    # Support the Video Audio (Hardware) Acceleration API
    gst_all_1.gst-vaapi
    libevdev
    libpulseaudio
    openssl
    curl
  ];
  # ++ lib.optionals cudaSupport [
  # cudaPackages.cudatoolkit
  # ] ++ lib.optionals stdenv.isx86_64 [
  # intel-media-sdk
  # ];

  cmakeFlags = [
    "-DFETCHCONTENT_SOURCE_DIR_IMMER=${deps.immer_src}"
    "-DFETCHCONTENT_SOURCE_DIR_EVENTBUS=${deps.eventbus_src}"
    "-DFETCHCONTENT_SOURCE_DIR_BOOST_JSON=${deps.boost-json_src}"
    "-DFETCHCONTENT_SOURCE_DIR_RANGE=${deps.range_src}"
    "-DFETCHCONTENT_SOURCE_DIR_FMTLIB=${deps.fmtlib_src}"
    "-DFETCHCONTENT_SOURCE_DIR_NANORS=${deps.nanors_src}"
    "-DFETCHCONTENT_SOURCE_DIR_PEGLIB=${deps.peglib_src}"
    "-DFETCHCONTENT_SOURCE_DIR_SIMPLEWEBSERVER=${deps.simplewebserver_src}"
    "-DFETCHCONTENT_SOURCE_DIR_TOML=${deps.toml_src}"
    "-DFETCHCONTENT_SOURCE_DIR_ENET=${deps.enet_src}"
    "-DFETCHCONTENT_SOURCE_DIR_CPPTRACE=${deps.cpptrace_src}"
    "-DFETCHCONTENT_SOURCE_DIR_LIBDWARF=${deps.libdwarf_src}"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_CXX_STANDARD=17"
    "-DCMAKE_CXX_EXTENSIONS=OFF"
    "-DBUILD_SHARED_LIBS=OFF"
    "-DBUILD_FAKE_UDEV_CLI=ON"
    "-DBUILD_TESTING=OFF"
    "-G Ninja"
  ];
  buildPhase = ''
    ninja wolf
  '';
  installPhase = ''
    mkdir -p $out/bin
    cp ./src/moonlight-server/wolf $out/bin/wolf
  '';
  postPatch = ''
    substituteInPlace src/moonlight-server/runners/docker.hpp --replace '"WOLF_DOCKER_FAKE_UDEV_PATH", ""' '"WOLF_DOCKER_FAKE_UDEV_PATH", "${fake-udev}/bin/fake-udev"' '';
}
