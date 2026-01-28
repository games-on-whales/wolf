{pkgs ? import <nixpkgs> {}}: 
  pkgs.mkShell {
    name = "Wolf";

    nativeBuildInputs = with pkgs; [
      docker
      docker-compose
    ];
  }
