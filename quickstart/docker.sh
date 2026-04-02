#!/usr/bin/env bash
# docker.sh -- Wolf deployment via Docker Compose
#
# Sourced by wolf.sh. Requires common.sh to be loaded first.

docker_main() {
    parse_args "$@"
    [[ $EUID -eq 0 ]] || err "Run as root"

    select_gpu

    info "Wolf Cloud Gaming Setup (Docker)"
    echo "  GPU:  $(selected_gpu_label)"
    echo "  Node: ${SELECTED_RENDER_NODE}"
    echo ""

    install_udev_rules

    [[ "$SELECTED_VENDOR" == "NVIDIA" ]] && build_nvidia_volume docker

    mkdir -p /etc/wolf /etc/wolf/wolf-den /etc/wolf/covers /opt/wolf

    info "Writing docker-compose.yml"
    write_compose "$SELECTED_VENDOR" "$SELECTED_RENDER_NODE"

    compose_start_wolf

    print_summary "Compose:   /opt/wolf/docker-compose.yml"
}
