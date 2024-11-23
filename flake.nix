{
  description = "wolf stream";
  inputs = {
    # nixpkgs.url = "github:NixOS/nixpkgs/9957cd48326fe8dbd52fdc50dd2502307f188b0d";
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    devenv.url = "github:cachix/devenv";
  };

  outputs = inputs@{ self, ... }:
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
          owner = "games-on-whales";
          repo = "eventbus";
          rev = "abb3a48";
          hash = "sha256-LHBsjvZtxid4KIFQclqs2I155J/9UpDR1NhlSFx4OvU=";
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
          rev = "5d4b8b2";
          hash = "sha256-piyGsI/BFTYJ9hVG+dw243ZDUiKZWdynJySGA6/IYlk=";
        };
        mdns-cpp_src = pkgs.fetchFromGitHub {
          owner = "games-on-whales";
          repo = "mdns_cpp";
          rev = "0d57ae3";
          hash = "sha256-mG/Ob5SIqcIyp5r5IpFh8bJOSul1zRzKvrvdfywVwcg=";
        };
        fmtlib_src = pkgs.fetchFromGitHub {
          owner = "fmtlib";
          repo = "fmt";
          rev = "11.0.1";
          hash = "sha256-EPidbZxCvysrL64AzbpJDowiNxqy4ii+qwSWAFwf/Ps=";
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
          rev = "19f07b513e924e471cadd141943c1ec4adc8d0e0";
          hash = "sha256-lpEDW5JZmFMPdJlS0/2a4MZU68dt7lz633ymbuSUyBc=";
        };
        peglib_src = pkgs.fetchFromGitHub {
          owner = "yhirose";
          repo = "cpp-peglib";
          rev = "v1.8.5";
          hash = "sha256-GeQQGJtxyoLAXrzplHbf2BORtRoTWrU08TWjjq7YqqE=";
        };
        # toml_src = pkgs.fetchFromGitHub {
        #   owner = "ToruNiina";
        #   repo = "toml11";
        #   rev = "v3.7.1";
        #   hash = "sha256-HnhXBvIjo1JXhp+hUQvjs83t5IBVbNN6o3ZGhB4WESQ=";
        # };
        tomlplusplus_src = pkgs.fetchFromGitHub {
          owner = "marzer";
          repo = "tomlplusplus";
          rev = "v3.4.0";
          hash = "sha256-h5tbO0Rv2tZezY58yUbyRVpsfRjY3i+5TPkkxr6La8M=";
        };
        cpptrace_src = pkgs.fetchFromGitHub {
          owner = "jeremy-rifkin";
          repo = "cpptrace";
          rev = "448c325";
          hash = "sha256-JGwRhmsd0xiHkK0JW0AUvWAnJA9UztK2wQ+c5aq2y6E=";
        };
        reflect-cpp_src = pkgs.fetchFromGitHub {
          owner = "getml";
          repo = "reflect-cpp";
          rev = "54c2a84";
          hash = "sha256-JLUH6LDEeWrEiVYXEMrqI5/y0HXFr0HH+iwtVHQ+qqk=";
        };
        libdwarf_src = pkgs.fetchFromGitHub {
          owner = "jeremy-rifkin";
          repo = "libdwarf-lite";
          rev = "v0.11.0";
          hash = "sha256-S2KDfWqqdQfK5+eQny2X5k0A5u9npkQ8OFRLBmTulao=";
        };
        simplewebserver_src = pkgs.fetchFromGitLab {
          owner = "eidheim";
          repo = "Simple-Web-Server";
          rev = "bdb1057";
          hash = "sha256-C9i/CyQG9QsDqIx75FbgiKp2b/POigUw71vh+rXAdyg=";
        };
        gst-interpipe_src = pkgs.fetchgit {
          # must use fetchgit.since repo in github & submodule in gitlab
          fetchSubmodules = true;
          url = "https://github.com/RidgeRun/gst-interpipe.git";
          hash = "sha256-Z+RgAsXawqAjNJeaqzoOcHF2xy45fVSo4jm2qIcbJ3o=";
        };
        gst-wayland-display =
          import ./nix/gst-wayland-display.nix { inherit pkgs; };
      };
    in {
      packages.x86_64-linux.gwd = deps.gst-wayland-display;
      packages.x86_64-linux.default =
        import ./nix/wolf.nix { inherit pkgs self deps; };

      devShells.x86_64-linux.default = inputs.devenv.lib.mkShell {
        inherit inputs pkgs;
        modules = [{
          env = { };
          packages = with pkgs; [ nil nixfmt deadnix ];
        }];
      };
    };
}
