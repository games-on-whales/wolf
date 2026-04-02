#!/usr/bin/env bash
# lxc.sh -- Wolf deployment on standalone LXC
#
# Sourced by wolf.sh. Requires common.sh to be loaded first.

lxc_main() {
    parse_args "$@"
    [[ $EUID -eq 0 ]] || err "Run as root"

    select_gpu

    info "Wolf Cloud Gaming Setup (LXC)"
    echo "  Name:  ${LXC_NAME}"
    echo "  CPU:   ${CT_CPU} cores"
    echo "  RAM:   ${CT_RAM} MB"
    echo "  Disk:  ${CT_DISK} GB"
    echo "  GPU:   $(selected_gpu_label)"
    echo ""

    if ! lxc-info -n "$LXC_NAME" &>/dev/null; then
        info "Creating container '$LXC_NAME'"
        lxc-create -t download -n "$LXC_NAME" \
            -B loop --fssize "${CT_DISK}G" \
            -- -d debian -r trixie -a amd64
    else
        info "SKIP: Container '$LXC_NAME' already exists"
    fi

    local conf="/var/lib/lxc/${LXC_NAME}/config"
    clean_lxc_gpu_config "$conf"

    # Resource limits
    cat >> "$conf" <<EOF

# Wolf cloud gaming -- resource limits
lxc.cgroup2.memory.max = ${CT_RAM}M
lxc.cgroup2.cpu.max = $((CT_CPU * 1000000)) 1000000
EOF
    write_lxc_gpu_config "$conf" " ="

    lxc-stop -n "$LXC_NAME" 2>/dev/null || true
    lxc-start -n "$LXC_NAME"
    sleep 3

    _lxc_push() {
        local rootfs="/var/lib/lxc/${LXC_NAME}/rootfs"
        cp "${SCRIPT_DIR}/common.sh" "${rootfs}/root/common.sh"
        chmod 755 "${rootfs}/root/common.sh"
        cp "${SCRIPT_DIR}/configure.sh" "${rootfs}/root/configure.sh"
        chmod 755 "${rootfs}/root/configure.sh"
    }
    _lxc_exec() { lxc-attach -n "$LXC_NAME" -- bash -c "$1"; }
    deploy_configure _lxc_exec _lxc_push
    info "Done"
}
