#!/usr/bin/env bash
# proxmox.sh -- Wolf deployment on Proxmox VE
#
# Sourced by wolf.sh. Requires common.sh to be loaded first.

# =========================================================================
# Storage selection
# =========================================================================

select_storage() {
    local storages=()
    local labels=()

    while IFS='|' read -r name type _ enabled; do
        [[ "$enabled" == "1" ]] || continue
        storages+=("$name")
        labels+=("${name} (${type})")
    done < <(pvesm status --content rootdir 2>/dev/null \
        | awk 'NR>1 {printf "%s|%s|rootdir|%s\n", $1, $2, ($3=="active"?"1":"0")}')

    [[ ${#storages[@]} -gt 0 ]] || err "No active Proxmox storage with rootdir content found"

    if [[ ${#storages[@]} -eq 1 ]]; then
        CT_STORAGE="${storages[0]}"
        info "Using storage: ${labels[0]}"
        return
    fi

    prompt_choice "Select storage for Wolf CT" "${labels[@]}"
    CT_STORAGE="${storages[$CHOICE_IDX]}"
    info "Selected storage: ${CT_STORAGE}"
}

# =========================================================================
# Network detection
# =========================================================================

detect_network() {
    local host_ip host_cidr
    read -r host_ip host_cidr < <(
        ip -4 -o addr show scope global | awk '{split($4,a,"/"); print a[1], a[2]; exit}'
    ) || true

    [[ -n "$host_ip" ]] || err "Could not detect a local IPv4 address. Specify --ip, --gw, and --cidr manually."

    if [[ "$CT_GW" == "auto" ]]; then
        CT_GW=$(ip -4 route show default | awk '{print $3; exit}') || true
        [[ -n "$CT_GW" ]] || err "Could not detect default gateway. Specify one with --gw."
        info "Detected gateway: ${CT_GW}"
    fi

    if [[ "$CT_CIDR" == "auto" ]]; then
        [[ -n "$host_cidr" ]] || err "Could not detect subnet prefix length. Specify one with --cidr."
        CT_CIDR="$host_cidr"
        info "Detected CIDR: /${CT_CIDR}"
    fi

    if [[ "$CT_IP" == "auto" ]]; then
        CT_IP="${host_ip%.*}.${CTID}"
        info "Derived container IP from host (${host_ip}): ${CT_IP}"
    fi
}

# =========================================================================
# Main
# =========================================================================

proxmox_main() {
    parse_args "$@"
    [[ $EUID -eq 0 ]] || err "Run as root"

    if [[ "$CT_IP" == "auto" || "$CT_GW" == "auto" || "$CT_CIDR" == "auto" ]]; then
        detect_network
    fi

    select_gpu

    info "Wolf Cloud Gaming Setup (Proxmox)"
    echo "  CTID:    ${CTID}"
    echo "  IP:      ${CT_IP}/${CT_CIDR}"
    echo "  Gateway: ${CT_GW}"
    echo "  CPU:     ${CT_CPU} cores"
    echo "  RAM:     ${CT_RAM} MB"
    echo "  Disk:    ${CT_DISK} GB"
    echo "  Storage: ${CT_STORAGE}"
    echo ""

    [[ "$CT_STORAGE" == "auto" ]] && select_storage

    # Create container
    if ! pct status "$CTID" &>/dev/null; then
        info "Creating CT $CTID (wolf) at $CT_IP"
        pct create "$CTID" "$TEMPLATE" \
            --hostname wolf \
            --memory "$CT_RAM" \
            --cores "$CT_CPU" \
            --rootfs "${CT_STORAGE}:${CT_DISK}" \
            --net0 "name=eth0,bridge=${LAN_BRIDGE},ip=${CT_IP}/${CT_CIDR},gw=${CT_GW}" \
            --unprivileged 0 \
            --features nesting=1 \
            --start 0 \
            || err "Failed to create CT $CTID"
        pct start "$CTID"
        sleep 3
    else
        info "SKIP: CT $CTID already exists"
    fi

    # Configure GPU passthrough (must stop container to edit config)
    pct stop "$CTID" 2>/dev/null || true
    sleep 2

    local conf="/etc/pve/lxc/${CTID}.conf"
    clean_lxc_gpu_config "$conf"
    write_lxc_gpu_config "$conf" ":"

    pct start "$CTID"
    sleep 3

    _pve_push() {
        pct push "$CTID" "${SCRIPT_DIR}/common.sh" /root/common.sh --perms 0755
        pct push "$CTID" "${SCRIPT_DIR}/configure.sh" /root/configure.sh --perms 0755
    }
    _pve_exec() { pct exec "$CTID" -- bash -c "$1"; }
    deploy_configure _pve_exec _pve_push
    info "Done"
}
