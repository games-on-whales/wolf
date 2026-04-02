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

    mkdir -p /etc/wolf/cfg /etc/wolf/wolf-den /etc/wolf/covers /etc/wolf/steam /opt/wolf

    write_wolf_config /etc/wolf/cfg

    info "Writing docker-compose.yml"
    write_compose "$SELECTED_VENDOR" "$SELECTED_RENDER_NODE"

    docker_start_wolf

    local ip; ip=$(get_local_ip)
    cat <<EOF

================================================================
Wolf cloud gaming is deployed (Docker).

  Wolf:      streaming on ports 47984-48200 (Moonlight)
  Wolf Den:  http://${ip}:8080 (web management)
  Compose:   /opt/wolf/docker-compose.yml
  GPU:       ${SELECTED_VENDOR} ${SELECTED_NAME} (${SELECTED_DRIVER}) at ${SELECTED_RENDER_NODE}

To pair with Moonlight:
  1. Open Wolf Den at http://${ip}:8080 to manage apps and clients
  2. Open Moonlight and add server: ${ip}
  3. Enter the pairing PIN shown in Moonlight into Wolf Den

Manage with:
  cd /opt/wolf
  docker compose stop       # stop
  docker compose restart    # restart
  docker compose logs -f    # view logs
  docker compose pull && docker compose up -d   # update
================================================================
EOF
}
