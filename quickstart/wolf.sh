#!/usr/bin/env bash
# wolf.sh -- Deploy Wolf cloud gaming with automatic environment detection
#
# Detects the runtime environment, downloads any missing helper scripts,
# and dispatches to the appropriate environment-specific module.
# All CLI options are passed through to the child scripts.
#
# Prereq: GPU drivers must be installed on the host
#
# Usage:
#   ./wolf.sh [OPTIONS]
#
# Run ./wolf.sh --help or see README.md for the full list of options.
# Options are environment-specific and defined in each module. Unknown
# flags for a given environment are silently ignored.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_URL="https://raw.githubusercontent.com/games-on-whales/wolf/stable/quickstart"

# =========================================================================
# Script fetching
# =========================================================================

# Download a file. Usage: fetch_url <url> <output_file>
fetch_url() {
    local url="$1" output="$2"
    if command -v curl &>/dev/null; then
        curl -fsSL "$url" -o "$output"
    elif command -v wget &>/dev/null; then
        wget -qO "$output" "$url"
    else
        echo "ERROR: Neither curl nor wget is available. Install one of them and try again." >&2
        exit 1
    fi
}

# Ensure a script exists locally, downloading it if needed.
# Usage: ensure_script <filename>
ensure_script() {
    local name="$1"
    local path="${SCRIPT_DIR}/${name}"

    if [[ -f "$path" ]]; then
        return
    fi

    echo "==> Downloading ${name}..."
    fetch_url "${BASE_URL}/${name}" "$path"
    chmod +x "$path"
}

# =========================================================================
# Environment detection
# =========================================================================

detect_environment() {
    command -v pveversion &>/dev/null && { echo "proxmox"; return; }
    command -v lxc-create &>/dev/null && { echo "lxc"; return; }
    command -v incus &>/dev/null     && { echo "incus"; return; }
    [[ -f /etc/unraid-version ]]     && { echo "unraid"; return; }
    command -v midclt &>/dev/null    && { echo "truenas"; return; }
    [[ -f /etc/NIXOS ]]             && { echo "nixos"; return; }
    command -v podman &>/dev/null    && { echo "podman"; return; }
    command -v docker &>/dev/null    && { echo "docker"; return; }

    echo "ERROR: Could not detect environment. Install Proxmox, LXC, Podman, Docker, or run on Unraid/TrueNAS/NixOS." >&2
    exit 1
}

# =========================================================================
# Main
# =========================================================================

ENVIRONMENT=$(detect_environment)

# Determine which scripts are needed for this environment
case "$ENVIRONMENT" in
    proxmox) NEEDED_SCRIPTS=(common.sh proxmox.sh configure.sh) ;;
    lxc)     NEEDED_SCRIPTS=(common.sh lxc.sh configure.sh) ;;
    incus)   NEEDED_SCRIPTS=(common.sh incus.sh configure.sh) ;;
    unraid)  NEEDED_SCRIPTS=(common.sh unraid.sh) ;;
    truenas) NEEDED_SCRIPTS=(common.sh truenas.sh) ;;
    nixos)   NEEDED_SCRIPTS=(common.sh nixos.sh) ;;
    podman)  NEEDED_SCRIPTS=(common.sh podman.sh) ;;
    docker)  NEEDED_SCRIPTS=(common.sh docker.sh) ;;
esac

# Fetch any missing scripts
for script in "${NEEDED_SCRIPTS[@]}"; do
    ensure_script "$script"
done

# Source common helpers and the environment-specific script, then dispatch.
# All CLI arguments are passed through to the child's main function, which
# calls parse_args to handle them.
source "${SCRIPT_DIR}/common.sh"

case "$ENVIRONMENT" in
    proxmox) source "${SCRIPT_DIR}/proxmox.sh"; proxmox_main "$@" ;;
    lxc)     source "${SCRIPT_DIR}/lxc.sh";     lxc_main "$@" ;;
    incus)   source "${SCRIPT_DIR}/incus.sh";    incus_main "$@" ;;
    unraid)  source "${SCRIPT_DIR}/unraid.sh";   unraid_main "$@" ;;
    truenas) source "${SCRIPT_DIR}/truenas.sh";  truenas_main "$@" ;;
    nixos)   source "${SCRIPT_DIR}/nixos.sh";    nixos_main "$@" ;;
    podman)  source "${SCRIPT_DIR}/podman.sh";   podman_main "$@" ;;
    docker)  source "${SCRIPT_DIR}/docker.sh";   docker_main "$@" ;;
esac
