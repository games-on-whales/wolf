{ pkgs, self, deps, ... }:
let
  wolf_folder = "/etc/wolf";
  wolf_cfg_folder = "${wolf_folder}/cfg";
  wolf_state_folder = "${wolf_folder}/state";
  fake-udev = pkgs.stdenv.mkDerivation {
    pname = "fake-udev";
    version = "1.0";
    src = "${self}/src/fake-udev";

    nativeBuildInputs = with pkgs; [ cmake pkg-config ninja autoPatchelfHook ];

    buildInputs = with pkgs; [ glibc.static ];

    cmakeFlags = [
      "-DCMAKE_BUILD_TYPE=Release"
      "-DCMAKE_CXX_STANDARD=17"
      "-DCMAKE_CXX_EXTENSIONS=OFF"
      "-DBUILD_FAKE_UDEV_CLI=ON"
      "-G Ninja"
    ];

    buildPhase = "ninja fake-udev";
    installPhase = ''
      mkdir -p $out/bin
      cp ./fake-udev $out/bin/fake-udev
    '';
  };
  gst-interpipe = pkgs.stdenv.mkDerivation {
    pname = "gst-interpipe";
    version = "1.0";
    src = deps.gst-interpipe_src;

    nativeBuildInputs = with pkgs; [
      meson
      ninja
      cmake
      pkg-config
      autoreconfHook
      gtk-doc
      docbook-xsl-nons
    ];

    buildInputs = with pkgs; [ gst_all_1.gstreamer gst_all_1.gst-plugins-base ];

    cmakeFlags = [ "-Denable-gtk-doc=false" ];
  };
in pkgs.stdenv.mkDerivation {
  pname = "wolf";
  version = "1.0";
  src = self;

  nativeBuildInputs = with pkgs; [ cmake pkg-config ninja wrapGAppsHook ];

  buildInputs = with pkgs; [
    fake-udev
    gst-interpipe
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
    boost186 # pin to this version due to use of boost/asio/io_service.hpp
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
    "-DFETCHCONTENT_SOURCE_DIR_INPUTTINO=${deps.inputtino_src}"
    "-DFETCHCONTENT_SOURCE_DIR_EVENTBUS=${deps.eventbus_src}"
    "-DFETCHCONTENT_SOURCE_DIR_BOOST_JSON=${deps.boost-json_src}"
    "-DFETCHCONTENT_SOURCE_DIR_RANGE=${deps.range_src}"
    "-DFETCHCONTENT_SOURCE_DIR_FMTLIB=${deps.fmtlib_src}"
    "-DFETCHCONTENT_SOURCE_DIR_NANORS=${deps.nanors_src}"
    "-DFETCHCONTENT_SOURCE_DIR_PEGLIB=${deps.peglib_src}"
    "-DFETCHCONTENT_SOURCE_DIR_SIMPLEWEBSERVER=${deps.simplewebserver_src}"
    # "-DFETCHCONTENT_SOURCE_DIR_TOML=${deps.toml_src}"
    "-DFETCHCONTENT_SOURCE_DIR_ENET=${deps.enet_src}"
    "-DFETCHCONTENT_SOURCE_DIR_CPPTRACE=${deps.cpptrace_src}"
    "-DFETCHCONTENT_SOURCE_DIR_LIBDWARF=${deps.libdwarf_src}"
    "-DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=${deps.tomlplusplus_src}"
    "-DFETCHCONTENT_SOURCE_DIR_REFLECT-CPP=${deps.reflect-cpp_src}"
    "-DFETCHCONTENT_SOURCE_DIR_MDNS_CPP=${deps.mdns-cpp_src}"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_CXX_STANDARD=17"
    "-DCMAKE_CXX_EXTENSIONS=OFF"
    "-DBUILD_SHARED_LIBS=OFF"
    "-DBUILD_FAKE_UDEV_CLI=ON"
    "-DBUILD_TESTING=OFF"
    "-G Ninja"
  ];

  preFixup = ''
       gappsWrapperArgs+=(
      --set-default XDG_RUNTIME_DIR "/tmp/sockets"
      --set-default WOLF_CFG_FOLDER  "${wolf_cfg_folder}"
      --set-default WOLF_CFG_FILE "${wolf_cfg_folder}/config.toml"
      --set-default WOLF_PRIVATE_KEY_FILE "${wolf_cfg_folder}/key.pem"
      --set-default WOLF_PRIVATE_CERT_FILE "${wolf_cfg_folder}/cert.pem"
      --set-default HOST_APPS_STATE_FOLDER "${wolf_state_folder}"
      --set-default WOLF_PULSE_IMAGE "ghcr.io/games-on-whales/pulseaudio:master"
      --set-default WOLF_DOCKER_SOCKET "/var/run/docker.sock"
      --set-default WOLF_DOCKER_FAKE_UDEV_PATH "${fake-udev}/bin/fake-udev"
      --set-default WOLF_RENDER_NODE "/dev/dri/renderD128"
      --set-default WOLF_ENCODER_NODE "/dev/dri/renderD128"
      --set-default WOLF_STOP_CONTAINER_ON_EXIT "TRUE"
      --set-default WOLF_LOG_LEVEL "INFO"
      --set-default RUST_BACKTRACE "full"
      --set-default RUST_LOG "WARN"
      --set-default GST_DEBUG 2
      --set-default PUID 0
      --set-default PGID 0
      --set-default UNAME "root"
    )
  '';

  buildPhase = "ninja wolf";
  installPhase = ''
    mkdir -p $out/bin
    cp ./src/moonlight-server/wolf $out/bin/wolf
  '';
  # postPatch = ''
  # substituteInPlace src/moonlight-server/runners/docker.hpp --replace '"WOLF_DOCKER_FAKE_UDEV_PATH", ""' '"WOLF_DOCKER_FAKE_UDEV_PATH", "${fake-udev}/bin/fake-udev"' '';
}
