{
  description = "wolf stream";
  inputs = {
    # nixpkgs.url = "github:NixOS/nixpkgs/9957cd48326fe8dbd52fdc50dd2502307f188b0d";
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = inputs@{ self,... }:
    let
    system = "x86_64-linux";
      pkgs = import inputs.nixpkgs { inherit system; };
      deps = {
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
        inputtino_src = pkgs.fetchFromGitHub {
          owner = "games-on-whales";
          repo = "inputtino";
          rev = "753a639";
          hash = "sha256-qw0qKNWPjO/VfFqNX3uhSniY+/KMIL8aggwyvQht9z8=";
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
        cpptrace_src = pkgs.fetchFromGitHub {
          owner = "jeremy-rifkin";
          repo = "cpptrace";
          rev = "448c325";
          hash = "sha256-JGwRhmsd0xiHkK0JW0AUvWAnJA9UztK2wQ+c5aq2y6E=";
        };
        libdwarf_src = pkgs.fetchFromGitHub {
          owner = "jeremy-rifkin";
          repo = "libdwarf-lite";
          rev = "5c0cb25";
          hash = "sha256-so/y6GnhwYZPFCeoKih+sPgndnuHWHE1h9mWXnccXxM=";
        };
        simplewebserver_src = pkgs.fetchFromGitLab {
          owner = "eidheim";
          repo = "Simple-Web-Server";
          rev = "bdb1057";
          hash = "sha256-C9i/CyQG9QsDqIx75FbgiKp2b/POigUw71vh+rXAdyg=";
        };
        gst-wayland-display =
          import ./nix/gst-wayland-display.nix { inherit pkgs; };
      };
    in {
      packages.x86_64-linux.gwd = deps.gst-wayland-display;
      packages.x86_64-linux.default =
        import ./nix/wolf.nix { inherit pkgs self deps; };

    };
}
