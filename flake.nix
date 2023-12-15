{
  description = "wolf stream";

  outputs = { self, nixpkgs }:
    let

      pkgs = nixpkgs.legacyPackages.x86_64-linux;
      boost-json_src = pkgs.fetchFromGitHub {
        owner = "boostorg";
        repo = "json";
        rev = "boost-1.75.0";
        hash = "sha256-c/spP97jrs6gfEzsiMpdt8DDP6n1qOQbLduY+1/i424=";
      };
      eventbus_src = pkgs.fetchFromGitHub {
        owner = "DeveloperPaul123";
        repo = "eventbus";
        rev = "0.10.1";
        hash = "sha256-q8ymvmgsKvU6i4DAsuzTzo7rsdGkRHqwmiLJ9qXcM/c=";
      };
      immer_src = pkgs.fetchFromGitHub {
        owner = "arximboldi";
        repo = "immer";
        rev = "e02cbd795e9424a8405a8cb01f659ad61c0cbbc7";
        hash = "sha256-buIaXxoJSTbqzsnxpd33BUCQtTGmdd10j1ArQd5rink=";
      };
      fmtlib_src = pkgs.fetchFromGitHub {
           owner = "fmtlib";
           repo = "fmt";
           rev = "9.1.0";
           hash = "sha256-rP6ymyRc7LnKxUXwPpzhHOQvpJkpnRFOt2ctvUNlYI0=";
         };
      range_src = pkgs.fetchFromGitHub {
          owner = "ericniebler";
          repo = "range-v3";
          rev = "0.12.0";
          hash = "sha256-bRSX91+ROqG1C3nB9HSQaKgLzOHEFy9mrD2WW3PRBWU=";
        };
      enet_src = pkgs.fetchFromGitHub {
        owner = "cgutman";
        repo = "enet";
        rev = "47e42dbf422396ce308a03b5a95ec056f0f0180c";
        hash = "sha256-ZAmkyDpdriEZUt4fs/daQFx5YqPYFTaU2GULWIN1AwI=";
      };
      nanors_src = pkgs.fetchFromGitHub {
        owner = "sleepybishop";
        repo = "nanors";
        rev = "395e5ada44dd8d5974eaf6bb6b17f23406e3ca72";
        hash = "sha256-M/jGBgQ64DTD7YPs+B4eRuArhOnUo8uPwJcviNu+GQk=";
      };
      peglib_src = pkgs.fetchFromGitHub {
        owner = "yhirose";
        repo = "cpp-peglib";
        rev = "v1.8.5";
        hash = "sha256-GeQQGJtxyoLAXrzplHbf2BORtRoTWrU08TWjjq7YqqE=";
      };
        toml_src = pkgs.fetchFromGitHub {
          owner = "ToruNiina";
          repo = "toml11";
          rev = "v3.7.1";
          hash = "sha256-HnhXBvIjo1JXhp+hUQvjs83t5IBVbNN6o3ZGhB4WESQ=";
        };
      simplewebserver_src = pkgs.fetchFromGitLab {
        owner = "eidheim";
        repo = "Simple-Web-Server";
        rev = "bdb1057";
        hash = "sha256-C9i/CyQG9QsDqIx75FbgiKp2b/POigUw71vh+rXAdyg=";
      };
      gstreamer-wolf = pkgs.stdenv.mkDerivation {
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
          depsBuildBuild =   with pkgs; [
    pkg-config
  ];
  strictDeps =true;
        nativeBuildInputs = with pkgs; [
          meson
          ninja
          /* cmake */
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
          /* boost175 */
          libcap
          gobject-introspection
          nasm # needed
                  qt6.qtbase
                  qt6.qttools
                  /* qt6.wrapQtAppsHook */
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
          /* flex */
          /* bison */
          /* python3 */
          /* bash-completion */
          /* wayland */
          /* wayland-protocols */
          /* libcap */
          /* zxing # is needed */
          /* zbar # is needed */
          /* libraspberrypi */
          libdrm
          libgudev
          /* libva */
          libtheora
          alsa-lib
          cdparanoia
          /* libintl */
          /* libcap */
          graphene
          libunwind
          elfutils
          gmp
          gsl
          /* gobject-introspection */
          /* xorg.libXext */
          xorg.libX11
          xorg.libXi
          xorg.libXv
          /* xorg.libXfixes */
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

          /**/
          /* #test */
          /* lcms */
          /* cairo */
          /* x264 */
          /* x265 */
          /* libaom */
          /* libwebp */
          /* resvg */
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
          propagatedBuildInputs = [
    pkgs.glib
  ];

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
    /* "-Dgst-plugins-good:rpi-lib-dir=${pkgs.libraspberrypi}/lib" */
    /* "-Dgst-plugins-good:rpi-header-dir=${pkgs.libraspberrypi}/lib" */
    ];

        /* ]++ (if raspiCameraSupport then [ */
    /* "-Drpi-lib-dir=${libraspberrypi}/lib" */
  /* ] else [ */
    /* "-Drpicamsrc=disabled" */
  /* ]); */
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
      };
      gst-wayland-display = pkgs.rustPlatform.buildRustPackage rec {
        pname = "gst-wayland-display";
        version = "1.0";

        src = pkgs.fetchFromGitHub {
          owner = "games-on-whales";
          repo = "gst-wayland-display";
          rev = "aa5626f7b74cc5aeeb2ded188fe62ca27b056998";
          hash = "sha256-QOTZDlcmjzQqDMheCx13NryfO1mP+AI8Lq2xS1IoG0Y=";
        };
        nativeBuildInputs = with pkgs; [ pkg-config cargo-c ];
        buildInputs = with pkgs; [
          glib
          wayland
          libinput
          libxkbcommon
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

          udev
        ];
        cargoLockFile = builtins.toFile "cargo.lock" (builtins.readFile "${src}/Cargo.lock");
        cargoLock = {
          lockFile = cargoLockFile;
          outputHashes = {
            # "smithay-0.3.0" = pkgs.lib.fakeSha256;
            "smithay-0.3.0" =
              "sha256-jrBY/r4IuVKiE7ykuxeZcJgikqJo6VoKQlBWrDbpy9Y=";
          };
        };
        postPatch = ''
          cp ${cargoLockFile} Cargo.lock
        '';
        buildPhase = ''
                    export HOME=$(mktemp -d)
          runHook preBuild
          ${pkgs.rust.envVars.setEnv} cargo cbuild --release --frozen --prefix=${
            placeholder "out"
          } --target ${pkgs.stdenv.hostPlatform.rust.rustcTarget}
          runHook postBuild
        '';
        # installPhase = "${pkgs.rust.envVars.setEnv} cargo cinstall --release --frozen --prefix=${placeholder "out"} --target ${pkgs.stdenv.hostPlatform.rust.rustcTarget}";
        installPhase = ''
          runHook preInstall
          ${pkgs.rust.envVars.setEnv} cargo cinstall --release --frozen --prefix=${
            placeholder "out"
          } --target ${pkgs.stdenv.hostPlatform.rust.rustcTarget}
          runHook postInstall
        '';
      };

    in {
      packages.x86_64-linux.gst = gstreamer-wolf;
      packages.x86_64-linux.gwd = gst-wayland-display;
      packages.x86_64-linux.default = pkgs.stdenv.mkDerivation {
        pname = "wolf";
        version = "1.0";
        src = self;
        patches = [];

        nativeBuildInputs = with pkgs; [ cmake pkg-config ninja ];

        buildInputs = with pkgs; [
          gst-wayland-display
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
          "-DFETCHCONTENT_SOURCE_DIR_IMMER=${immer_src}"
          "-DFETCHCONTENT_SOURCE_DIR_EVENTBUS=${eventbus_src}"
          "-DFETCHCONTENT_SOURCE_DIR_BOOST_JSON=${boost-json_src}"
          "-DFETCHCONTENT_SOURCE_DIR_RANGE=${range_src}"
          "-DFETCHCONTENT_SOURCE_DIR_FMTLIB=${fmtlib_src}"
          "-DFETCHCONTENT_SOURCE_DIR_NANORS=${nanors_src}"
          "-DFETCHCONTENT_SOURCE_DIR_PEGLIB=${peglib_src}"
          "-DFETCHCONTENT_SOURCE_DIR_SIMPLEWEBSERVER=${simplewebserver_src}"
          "-DFETCHCONTENT_SOURCE_DIR_TOML=${toml_src}"
          "-DFETCHCONTENT_SOURCE_DIR_ENET=${enet_src}"
          "-DCMAKE_BUILD_TYPE=Release"
          "-DCMAKE_CXX_STANDARD=17"
          "-DCMAKE_CXX_EXTENSIONS=OFF"
          "-DBUILD_SHARED_LIBS=OFF"
          "-DBUILD_FAKE_UDEV_CLI=ON"
          "-DBUILD_TESTING=OFF"
          "-G Ninja"
        ];
        buildPhase = ''
          # mkdir -p $out/bin
          ninja wolf
          # ninja fake-udev
          # cp -r $src/src $out/bin
          # cp ./src/moonlight-server/wolf $out/bin/wolf
        '';
        installPhase = ''
          mkdir -p $out/bin
          # cp -r $src/src $out/bin
          cp ./src/moonlight-server/wolf $out/bin/wolf
          # cp ./src/fake-udev/fake-udev $out/bin/fake-udev
        '';
      };

    };
}