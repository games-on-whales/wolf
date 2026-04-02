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

    # Set SELECTED_* for print_summary (configure.sh receives these as env vars)
    SELECTED_VENDOR="$gpu_vendor"
    SELECTED_NAME="$gpu_name"
    SELECTED_DRIVER="$gpu_driver"
    SELECTED_RENDER_NODE="$render_node"
    print_summary
}

configure
