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

    # Install Docker if not present (this runs inside LXC containers created
    # by our scripts, so it's safe to auto-install). Uses Docker's official
    # install script to avoid maintaining apt repo setup ourselves.
    if ! command -v docker &>/dev/null; then
        info "Installing Docker via official install script"
        curl -fsSL https://get.docker.com | sh
        info "Docker installed"
    else
        info "Docker already installed"
    fi

    install_udev_rules

    mkdir -p /etc/wolf /etc/wolf/wolf-den /etc/wolf/covers /opt/wolf

    info "Writing docker-compose.yml for ${gpu_vendor}"
    write_compose "$gpu_vendor" "$render_node"

    if [[ "$gpu_vendor" == "NVIDIA" ]]; then
        NV_VERSION="${WOLF_NV_VERSION:?WOLF_NV_VERSION not set}"
        build_nvidia_volume docker
    fi

    compose_start_wolf

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
