{ pkgs, ... }:

pkgs.rustPlatform.buildRustPackage rec {
  pname = "gst-wayland-display";
  version = "1.0";

  src = pkgs.fetchFromGitHub {
    owner = "games-on-whales";
    repo = "gst-wayland-display";
    rev = "a31f5a02a1c54ee14fca54f1eaea1a1c583ab139";
    hash = "sha256-xofDFqIjSEdzXj3/Qa2G24GZcLArOrwIoBSKqteqBLE=";
  };

  nativeBuildInputs = with pkgs; [ pkg-config cargo-c ];

  buildInputs = with pkgs; [
    mesa
    libglvnd
    pipewire
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
    # Support the Video Audio (Hardware) Acceleration API
    gst_all_1.gst-vaapi

    udev
  ];

  doCheck = false; # Disables test checks

  cargoLockFile =
    builtins.toFile "cargo.lock" (builtins.readFile "${src}/Cargo.lock");
  cargoLock = {
    lockFile = cargoLockFile;
    outputHashes = {
      # "smithay-0.3.0" = pkgs.lib.fakeSha256;
      "smithay-0.3.0" = "sha256-d13BZvEWSwKzFVe7X9ysCNQZj6BFPChM4oCfvX7URs8=";
    };
  };

  # Force linking to libEGL, which is always dlopen()ed, and to
  # libwayland-client, which is always dlopen()ed except by the
  # obscure winit backend.
  RUSTFLAGS = map (a: "-C link-arg=${a}") [
    "-Wl,--push-state,--no-as-needed"
    "-lEGL"
    "-lwayland-client"
    "-Wl,--pop-state"
  ];

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
