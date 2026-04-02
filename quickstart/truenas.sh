#!/usr/bin/env bash
# truenas.sh -- Wolf deployment on TrueNAS SCALE
#
# Sourced by wolf.sh. Requires common.sh to be loaded first.

# =========================================================================
# ZFS pool selection
# =========================================================================

# Auto-detect or validate ZFS pool. Sets ZFS_POOL.
select_pool() {
    local pools=()
    local labels=()

    while read -r name size alloc; do
        pools+=("$name")
        labels+=("${name} (${size}, ${alloc} used)")
    done < <(zpool list -Ho name,size,alloc 2>/dev/null)

    [[ ${#pools[@]} -gt 0 ]] || err "No ZFS pools found. TrueNAS requires at least one storage pool."

    if [[ "$ZFS_POOL" != "auto" ]]; then
        local i
        for i in "${!pools[@]}"; do
            if [[ "${pools[$i]}" == "$ZFS_POOL" ]]; then
                info "Using pool: ${labels[$i]}"
                return
            fi
        done
        err "Pool '${ZFS_POOL}' not found. Available: ${pools[*]}"
    fi

    if [[ ${#pools[@]} -eq 1 ]]; then
        ZFS_POOL="${pools[0]}"
        info "Detected pool: ${labels[0]}"
        return
    fi

    prompt_choice "Select ZFS pool for Wolf appdata" "${labels[@]}"
    ZFS_POOL="${pools[$CHOICE_IDX]}"
    info "Selected pool: ${ZFS_POOL}"
}

# =========================================================================
# Init script management
# =========================================================================

# Write a self-contained init script to the ZFS dataset. This script restores
# udev rules and starts Wolf on boot. It is registered with TrueNAS via midclt
# so it survives system updates (unlike files in /etc/).
write_truenas_init_script() {
    local appdata="$1" compose_file="$2" rules_src="$3"

    local init_script="${appdata}/wolf-init.sh"
    cat > "$init_script" <<INITEOF
#!/usr/bin/env bash
# Wolf cloud gaming -- TrueNAS POSTINIT script
# Restores udev rules and starts Wolf on boot.

# Restore udev rules (system partition is overwritten on updates)
cp "${rules_src}" /etc/udev/rules.d/85-wolf-virtual-inputs.rules
udevadm control --reload-rules 2>/dev/null || true
udevadm trigger 2>/dev/null || true

# Start Wolf
docker compose -f "${compose_file}" up -d &
INITEOF
    chmod 755 "$init_script"
}

# Register the init script with TrueNAS via midclt. Idempotent: checks for
# an existing Wolf init script before creating a new one.
register_truenas_init() {
    local init_script="$1"

    # Check if we already registered a Wolf init script
    local existing
    existing=$(midclt call initshutdownscript.query \
        '[["script", "~", "wolf-init.sh"]]' 2>/dev/null) || true

    if [[ -n "$existing" && "$existing" != "[]" ]]; then
        info "TrueNAS init script already registered, updating path"
        local script_id
        script_id=$(echo "$existing" | python3 -c "import sys,json; print(json.load(sys.stdin)[0]['id'])" 2>/dev/null) || true
        if [[ -n "$script_id" ]]; then
            midclt call initshutdownscript.update "$script_id" \
                "{\"script\": \"${init_script}\", \"when\": \"POSTINIT\", \"enabled\": true, \"type\": \"SCRIPT\"}" \
                >/dev/null
            return
        fi
    fi

    info "Registering init script with TrueNAS"
    midclt call initshutdownscript.create \
        "{\"type\": \"SCRIPT\", \"script\": \"${init_script}\", \"when\": \"POSTINIT\", \"enabled\": true}" \
        >/dev/null
}

# =========================================================================
# Main
# =========================================================================

truenas_main() {
    parse_args "$@"
    [[ $EUID -eq 0 ]] || err "Run as root"

    command -v docker &>/dev/null \
        || err "Docker is not available. TrueNAS SCALE Electric Eel (24.10+) is required for Docker support."
    command -v midclt &>/dev/null \
        || err "midclt not found. This script supports TrueNAS SCALE only (not TrueNAS CORE)."

    select_gpu

    # Resolve appdata path: if --appdata was not explicitly set, derive from pool
    if [[ "$APPDATA" == "/mnt/user/appdata/wolf" ]]; then
        select_pool
        APPDATA="/mnt/${ZFS_POOL}/appdata/wolf"
    fi

    local wolf_dir="${APPDATA}/wolf"
    local wolf_den_dir="${APPDATA}/wolf-den"
    local covers_dir="${APPDATA}/covers"
    local compose_dir="${APPDATA}"
    local rules_src="${APPDATA}/wolf-virtual-inputs.rules"

    info "Wolf Cloud Gaming Setup (TrueNAS SCALE)"
    echo "  Appdata: ${APPDATA}"
    echo "  GPU:     $(selected_gpu_label)"
    echo "  Node:    ${SELECTED_RENDER_NODE}"
    echo ""

    mkdir -p "$wolf_dir" "$wolf_den_dir" "$covers_dir"

    # Write udev rules to the ZFS dataset (persistent) and install live
    info "Setting up udev rules for virtual input"
    write_udev_rules_content > "$rules_src"
    cp "$rules_src" /etc/udev/rules.d/85-wolf-virtual-inputs.rules
    udevadm control --reload-rules 2>/dev/null || true
    udevadm trigger 2>/dev/null || true

    local compose_file="${compose_dir}/docker-compose.yml"

    info "Writing docker-compose.yml for ${SELECTED_VENDOR}"
    write_compose "$SELECTED_VENDOR" "$SELECTED_RENDER_NODE" \
        "$compose_file" "$wolf_dir" "$wolf_den_dir" "$covers_dir"

    if [[ "$SELECTED_VENDOR" == "NVIDIA" ]]; then
        detect_nvidia_version
        build_nvidia_volume docker
    fi

    # Write and register the boot init script
    write_truenas_init_script "$APPDATA" "$compose_file" "$rules_src"
    register_truenas_init "${APPDATA}/wolf-init.sh"

    compose_start_wolf "$compose_file"

    print_summary \
        "Compose:   ${compose_file}" \
        "Appdata:   ${APPDATA}" \
        "" \
        "Persistence: init script registered with TrueNAS (survives updates)"
}
