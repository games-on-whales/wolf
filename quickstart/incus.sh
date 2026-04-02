#!/usr/bin/env bash
# incus.sh -- Wolf deployment on Incus
#
# Sourced by wolf.sh. Requires common.sh to be loaded first.

incus_main() {
    parse_args "$@"
    [[ $EUID -eq 0 ]] || err "Run as root"

    select_gpu

    info "Wolf Cloud Gaming Setup (Incus)"
    echo "  Name:  ${LXC_NAME}"
    echo "  CPU:   ${CT_CPU} cores"
    echo "  RAM:   ${CT_RAM} MB"
    echo "  Disk:  ${CT_DISK} GB"
    echo "  GPU:   $(selected_gpu_label)"
    echo ""

    if ! incus info "$LXC_NAME" &>/dev/null; then
        info "Creating container '$LXC_NAME'"
        incus launch images:debian/trixie "$LXC_NAME" \
            -c security.privileged=true \
            -c security.nesting=true \
            -c limits.cpu="$CT_CPU" \
            -c limits.memory="${CT_RAM}MiB" \
            -d root,size="${CT_DISK}GiB"
    else
        info "SKIP: Container '$LXC_NAME' already exists"
    fi

    info "Configuring GPU passthrough"

    # Remove old devices first so re-runs apply the latest config
    for dev in gpu-dri uinput uhid nvidia0 nvidiactl nvidia-modeset \
               nvidia-uvm nvidia-uvm-tools nvidia-caps kfd; do
        incus config device remove "$LXC_NAME" "$dev" 2>/dev/null || true
    done

    incus config device add "$LXC_NAME" gpu-dri disk \
        source=/dev/dri path=/dev/dri
    incus config device add "$LXC_NAME" uinput unix-char \
        source=/dev/uinput path=/dev/uinput
    incus config device add "$LXC_NAME" uhid unix-char \
        source=/dev/uhid path=/dev/uhid

    case "$SELECTED_VENDOR" in
        NVIDIA)
            for dev in nvidia0 nvidiactl nvidia-modeset nvidia-uvm nvidia-uvm-tools; do
                [[ -e "/dev/$dev" ]] || continue
                incus config device add "$LXC_NAME" "$dev" unix-char \
                    source="/dev/$dev" path="/dev/$dev"
            done
            [[ -d /dev/nvidia-caps ]] && incus config device add "$LXC_NAME" nvidia-caps disk \
                source=/dev/nvidia-caps path=/dev/nvidia-caps
            ;;
        AMD)
            [[ -e /dev/kfd ]] && incus config device add "$LXC_NAME" kfd unix-char \
                source=/dev/kfd path=/dev/kfd
            ;;
    esac

    incus start "$LXC_NAME" 2>/dev/null || true
    sleep 3

    _incus_push() {
        incus file push "${SCRIPT_DIR}/common.sh" "${LXC_NAME}/root/common.sh" --mode 0755
        incus file push "${SCRIPT_DIR}/configure.sh" "${LXC_NAME}/root/configure.sh" --mode 0755
    }
    _incus_exec() { incus exec "$LXC_NAME" -- bash -c "$1"; }
    deploy_configure _incus_exec _incus_push
    info "Done"
}
