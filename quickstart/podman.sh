#!/usr/bin/env bash
# podman.sh -- Wolf deployment via Podman Quadlets
#
# Sourced by wolf.sh. Requires common.sh to be loaded first.

# =========================================================================
# Quadlet generation
# =========================================================================

_write_wolf_quadlet() {
    local nvidia_devices="" nvidia_volumes="" nvidia_env=""
    if [[ "$SELECTED_VENDOR" == "NVIDIA" ]]; then
        nvidia_devices="AddDevice=/dev/nvidia-uvm
AddDevice=/dev/nvidia-uvm-tools
AddDevice=/dev/nvidia-caps/nvidia-cap1
AddDevice=/dev/nvidia-caps/nvidia-cap2
AddDevice=/dev/nvidiactl
AddDevice=/dev/nvidia0
AddDevice=/dev/nvidia-modeset"
        nvidia_volumes="Volume=nvidia-driver-vol:/usr/nvidia"
        nvidia_env="Environment=NVIDIA_DRIVER_VOLUME_NAME=nvidia-driver-vol"
    fi

    cat <<QUADLET
[Unit]
Description=Wolf Cloud Gaming (Games On Whales)
Requires=network-online.target podman.socket
After=network-online.target podman.socket

[Service]
TimeoutStartSec=900
ExecStartPre=-/usr/bin/podman rm --force WolfPulseAudio
Restart=on-failure
RestartSec=5
StartLimitBurst=5

[Container]
AutoUpdate=registry
ContainerName=%p
HostName=%p
Image=ghcr.io/games-on-whales/wolf:stable
Network=host
SecurityLabelDisable=true
PodmanArgs=--ipc=host --device-cgroup-rule "c 13:* rmw"
AddDevice=/dev/dri
AddDevice=/dev/uinput
AddDevice=/dev/uhid
${nvidia_devices:+${nvidia_devices}
}${nvidia_volumes:+${nvidia_volumes}
}Volume=/dev/:/dev/:rw
Volume=/run/udev:/run/udev:rw
Volume=/etc/wolf/:/etc/wolf:z
Volume=/run/podman/podman.sock:/var/run/docker.sock:ro
Volume=wolf-socket:/tmp/sockets
Environment=WOLF_STOP_CONTAINER_ON_EXIT=TRUE
Environment=XDG_RUNTIME_DIR=/tmp/sockets
${nvidia_env:+${nvidia_env}
}Environment=WOLF_RENDER_NODE=${SELECTED_RENDER_NODE}

[Install]
WantedBy=multi-user.target
QUADLET
}

_write_wolf_den_quadlet() {
    cat <<QUADLET
[Unit]
Description=Wolf Den Web Management (Games On Whales)
Requires=wolf.service
After=wolf.service

[Service]
Restart=on-failure
RestartSec=5

[Container]
AutoUpdate=registry
ContainerName=%p
Image=ghcr.io/games-on-whales/wolf-den:stable
PublishPort=8080:8080
Volume=wolf-socket:/tmp/sockets
Volume=/etc/wolf/wolf-den:/app/wolf-den:z
Volume=/etc/wolf/covers:/etc/wolf/covers:z
Environment=WOLF_SOCKET_PATH=/tmp/sockets/wolf.sock

[Install]
WantedBy=multi-user.target
QUADLET
}

# =========================================================================
# Main
# =========================================================================

podman_main() {
    parse_args "$@"
    [[ $EUID -eq 0 ]] || err "Run as root"

    select_gpu

    info "Wolf Cloud Gaming Setup (Podman Quadlet)"
    echo "  GPU:  $(selected_gpu_label)"
    echo "  Node: ${SELECTED_RENDER_NODE}"
    echo ""

    install_udev_rules

    info "Enabling Podman socket"
    systemctl enable --now podman.socket

    [[ "$SELECTED_VENDOR" == "NVIDIA" ]] && build_nvidia_volume podman

    mkdir -p /etc/wolf/wolf-den /etc/wolf/covers
    local quadlet_dir="/etc/containers/systemd"
    mkdir -p "$quadlet_dir"

    info "Writing Podman Quadlets"
    _write_wolf_quadlet > "${quadlet_dir}/wolf.container"
    _write_wolf_den_quadlet > "${quadlet_dir}/wolf-den.container"

    # Create the shared socket volume
    podman volume create wolf-socket 2>/dev/null || true

    info "Reloading systemd and starting Wolf + Wolf Den"
    systemctl daemon-reload
    systemctl enable wolf.service wolf-den.service
    # restart (not just enable --now) so updated Quadlet config is applied on re-runs
    systemctl restart wolf.service wolf-den.service

    sleep 5
    local running=true
    systemctl is-active --quiet wolf.service || running=false
    systemctl is-active --quiet wolf-den.service || running=false
    if $running; then
        info "Services are running"
    else
        warn "Some services may not be running yet. Check: systemctl status wolf wolf-den"
    fi

    local ip; ip=$(get_local_ip)
    cat <<EOF

================================================================
Wolf cloud gaming is deployed (Podman Quadlet).

  Wolf:      streaming on ports 47984-48200 (Moonlight)
  Wolf Den:  http://${ip}:8080 (web management)
  Services:  systemctl status wolf wolf-den
  GPU:       ${SELECTED_VENDOR} ${SELECTED_NAME} (${SELECTED_DRIVER}) at ${SELECTED_RENDER_NODE}

To pair with Moonlight:
  1. Open Wolf Den at http://${ip}:8080 to manage apps and clients
  2. Open Moonlight and add server: ${ip}
  3. Enter the pairing PIN shown in Moonlight into Wolf Den

Manage with:
  systemctl stop wolf wolf-den       # stop
  systemctl restart wolf wolf-den    # restart
  journalctl -u wolf -f              # view Wolf logs
  journalctl -u wolf-den -f          # view Wolf Den logs
================================================================
EOF
}
