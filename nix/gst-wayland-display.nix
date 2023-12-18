{ pkgs, ... }:
pkgs.rustPlatform.buildRustPackage rec {
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
  cargoLockFile =
    builtins.toFile "cargo.lock" (builtins.readFile "${src}/Cargo.lock");
  cargoLock = {
    lockFile = cargoLockFile;
    outputHashes = {
      # "smithay-0.3.0" = pkgs.lib.fakeSha256;
      "smithay-0.3.0" = "sha256-jrBY/r4IuVKiE7ykuxeZcJgikqJo6VoKQlBWrDbpy9Y=";
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
}
