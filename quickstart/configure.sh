#!/usr/bin/env bash
# configure.sh -- Container-side Wolf configuration
#
# Runs inside an LXC container (pushed by Proxmox, LXC, or Incus scripts).
# Sources common.sh from the same directory for shared helpers.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

configure() {
    local gpu_vendor="${WOLF_GPU_VENDOR:?WOLF_GPU_VENDOR not set}"
    local gpu_driver="${WOLF_GPU_DRIVER:?WOLF_GPU_DRIVER not set}"
    local gpu_name="${WOLF_GPU_NAME:-Unknown}"
    local render_node="${WOLF_RENDER_NODE:-/dev/dri/renderD128}"

    info "Configuring Wolf for ${gpu_vendor} ${gpu_name} (${gpu_driver}, ${render_node})"

    # Install Docker
    if ! command -v docker &>/dev/null; then
        info "Installing Docker"
        apt-get update -qq
        apt-get install -y --no-install-recommends ca-certificates curl gnupg

        install -m 0755 -d /etc/apt/keyrings
        curl -fsSL https://download.docker.com/linux/debian/gpg \
            | gpg --dearmor -o /etc/apt/keyrings/docker.gpg
        chmod a+r /etc/apt/keyrings/docker.gpg

        echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] \
https://download.docker.com/linux/debian $(. /etc/os-release && echo "$VERSION_CODENAME") stable" \
            > /etc/apt/sources.list.d/docker.list

        apt-get update -qq
        apt-get install -y --no-install-recommends \
            docker-ce docker-ce-cli containerd.io docker-compose-plugin
        info "Docker installed"
    else
        info "Docker already installed"
    fi

    install_udev_rules

    mkdir -p /etc/wolf/cfg /etc/wolf/wolf-den /etc/wolf/covers /etc/wolf/steam /opt/wolf

    write_wolf_config /etc/wolf/cfg

    info "Writing docker-compose.yml for ${gpu_vendor}"
    write_compose "$gpu_vendor" "$render_node"

    if [[ "$gpu_vendor" == "NVIDIA" ]]; then
        NV_VERSION="${WOLF_NV_VERSION:?WOLF_NV_VERSION not set}"
        build_nvidia_volume docker
    fi

    docker_start_wolf

    local ip; ip=$(get_local_ip)
    cat <<EOF

================================================================
Wolf cloud gaming is deployed.

  Wolf:      streaming on ports 47984-48200 (Moonlight)
  Wolf Den:  http://${ip}:8080 (web management)
  GPU:       ${gpu_vendor} ${gpu_name} (${gpu_driver}) at ${render_node}

To pair with Moonlight:
  1. Open Wolf Den at http://${ip}:8080 to manage apps and clients
  2. Open Moonlight and add server: ${ip}
  3. Enter the pairing PIN shown in Moonlight into Wolf Den
================================================================
EOF
}

configure
