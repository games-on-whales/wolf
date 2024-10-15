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

  preFixup = ''
       gappsWrapperArgs+=(
      --prefix XDG_RUNTIME_DIR : "/tmp/sockets"
      --prefix WOLF_CFG_FOLDER  : "${wolf_cfg_folder}"
      --prefix WOLF_CFG_FILE : "${wolf_cfg_folder}/config.toml"
      --prefix WOLF_PRIVATE_KEY_FILE : "${wolf_cfg_folder}/key.pem"
      --prefix WOLF_PRIVATE_CERT_FILE : "${wolf_cfg_folder}/cert.pem"
      --prefix HOST_APPS_STATE_FOLDER : "${wolf_state_folder}"
      --prefix WOLF_PULSE_IMAGE : "ghcr.io/games-on-whales/pulseaudio:master"
      --prefix WOLF_DOCKER_SOCKET : "/var/run/docker.sock"
      --prefix WOLF_RENDER_NODE : "/dev/dri/renderD128"
      --prefix WOLF_STOP_CONTAINER_ON_EXIT : "TRUE"
      --prefix WOLF_LOG_LEVEL : "INFO"
      --prefix RUST_BACKTRACE : "full"
      --prefix RUST_LOG : "WARN"
      --prefix GST_DEBUG : 2
      --prefix PUID : 0 
      --prefix PGID : 0 
      --prefix UNAME : "root"
    )
  '';

  buildPhase = "ninja wolf";
  installPhase = ''
    mkdir -p $out/bin
    cp ./src/moonlight-server/wolf $out/bin/wolf
  '';
  postPatch = ''
    substituteInPlace src/moonlight-server/runners/docker.hpp --replace '"WOLF_DOCKER_FAKE_UDEV_PATH", ""' '"WOLF_DOCKER_FAKE_UDEV_PATH", "${fake-udev}/bin/fake-udev"' '';
}
