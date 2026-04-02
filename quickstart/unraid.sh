#!/usr/bin/env bash
# unraid.sh -- Wolf deployment on Unraid
#
# Sourced by wolf.sh. Requires common.sh to be loaded first.

# Install udev rules persistently on Unraid. The root filesystem is a tmpfs,
# so rules written to /etc/ are lost on reboot. Unraid's convention is to
# store custom udev rules in /boot/config/ and restore them via /boot/config/go.
install_udev_rules_unraid() {
    local rules_src="/boot/config/wolf-virtual-inputs.rules"
    local rules_dst="/etc/udev/rules.d/85-wolf-virtual-inputs.rules"

    info "Setting up persistent udev rules for virtual input"

    # Write the canonical copy to the flash drive
    write_udev_rules_content > "$rules_src"

    # Copy into the live tmpfs so it takes effect immediately
    cp "$rules_src" "$rules_dst"
    udevadm control --reload-rules 2>/dev/null || true
    udevadm trigger 2>/dev/null || true

    # Ensure /boot/config/go restores the rules on every boot.
    # The go script runs at the end of Unraid's boot process.
    local go="/boot/config/go"
    local marker="# Wolf udev rules"
    if ! grep -qF "$marker" "$go" 2>/dev/null; then
        info "Adding udev restore to /boot/config/go"
        cat >> "$go" <<EOF

$marker
cp ${rules_src} ${rules_dst}
udevadm control --reload-rules 2>/dev/null || true
udevadm trigger 2>/dev/null || true
EOF
    fi
}

unraid_main() {
    parse_args "$@"
    [[ $EUID -eq 0 ]] || err "Run as root"

    command -v docker &>/dev/null || err "Docker is not available. Enable Docker in Unraid Settings > Docker."

    select_gpu

    local wolf_dir="${APPDATA}/wolf"
    local wolf_den_dir="${APPDATA}/wolf-den"
    local covers_dir="${APPDATA}/covers"
    local compose_dir="${APPDATA}"

    info "Wolf Cloud Gaming Setup (Unraid)"
    echo "  Appdata: ${APPDATA}"
    echo "  GPU:     $(selected_gpu_label)"
    echo "  Node:    ${SELECTED_RENDER_NODE}"
    echo ""

    install_udev_rules_unraid

    mkdir -p "$wolf_dir" "$wolf_den_dir" "$covers_dir"

    local compose_file="${compose_dir}/docker-compose.yml"

    info "Writing docker-compose.yml for ${SELECTED_VENDOR}"
    write_compose "$SELECTED_VENDOR" "$SELECTED_RENDER_NODE" \
        "$compose_file" "$wolf_dir" "$wolf_den_dir" "$covers_dir"

    if [[ "$SELECTED_VENDOR" == "NVIDIA" ]]; then
        detect_nvidia_version
        build_nvidia_volume docker
    fi

    compose_start_wolf "$compose_file"

    # Ensure Wolf starts on boot via /boot/config/go
    local go="/boot/config/go"
    local marker="# Wolf docker-compose"
    if ! grep -qF "$marker" "$go" 2>/dev/null; then
        info "Adding Wolf auto-start to /boot/config/go"
        cat >> "$go" <<EOF

$marker
docker compose -f ${compose_file} up -d &
EOF
    fi

    print_summary \
        "Compose:   ${compose_file}" \
        "Appdata:   ${APPDATA}" \
        "" \
        "Persistence: udev rules and auto-start saved to /boot/config/go"
}
