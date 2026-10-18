#!/usr/bin/env bash
#
# wolf-input-diag.sh — validate controller/input passthrough into Wolf containers.
#
# Answers two questions the operator actually cares about:
#   (A) ISOLATION  — is one session's controller input leaking into another
#                    container (e.g. a Steam app's pad also driving the Wolf UI)?
#   (B) DELIVERY   — is the controller actually producing events inside the
#                    container it is *supposed* to be in?
#
# How it works (black-box, host-side — no Wolf source or API needed):
#   * Wolf builds each session's virtual gamepad/mouse/keyboard as a distinct
#     evdev/uhid device with a unique MAJOR:MINOR, and mknod's it into ONLY the
#     matching session_id's container. So:
#       - the SAME major:minor visible in >1 container  => structural leak  (A)
#       - actuating one pad and seeing events land in a NON-active container
#                                                       => runtime leak     (A)
#       - actuating and seeing NO events in the active container
#                                                       => delivery failure (B)
#   * Device *identity* (name) is read from the host kernel via /sys/class/input,
#     joined to per-container nodes by major:minor.
#   * Reads are non-destructive: we never EVIOCGRAB, so the game keeps its input.
#
# Requirements on the Wolf HOST: bash, docker (or set DOCKER=podman), coreutils.
# No evtest / jq / evemu needed.
#
# Usage:
#   wolf-input-diag.sh host                  # HOST-LEAK audit: are Wolf pads reachable by the desktop user?
#   wolf-input-diag.sh discover              # list Wolf containers + their input devices
#   wolf-input-diag.sh static                # isolation check: any node shared across containers?
#   wolf-input-diag.sh monitor [SECS]        # live: press pads, watch which container lights up
#   wolf-input-diag.sh live <container> [SECS]   # targeted A+B test against one container
#   wolf-input-diag.sh all                   # host + discover + static, then hint at live tests
#
# The `host` audit is the one that catches the real-world "DualSense input leaks
# into host" class of bug (games-on-whales/wolf#451): a Wolf virtual device whose
# name is NOT matched by /etc/udev/rules.d/85-wolf.rules keeps logind's default
# `uaccess` ACL, so the host's logged-in desktop user can read the controller.
#
set -euo pipefail

DOCKER="${DOCKER:-docker}"
RULES_FILE="${RULES_FILE:-/etc/udev/rules.d/85-wolf.rules}"
DUR_DEFAULT=6

die()  { printf '\033[31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }
info() { printf '\033[36m%s\033[0m\n' "$*"; }
ok()   { printf '  \033[32m✓ %s\033[0m\n' "$*"; }
warn() { printf '  \033[33m! %s\033[0m\n' "$*"; }
bad()  { printf '  \033[31m✗ %s\033[0m\n' "$*"; }

need_docker() { command -v "$DOCKER" >/dev/null 2>&1 || die "'$DOCKER' not found. Run this on the Wolf host (set DOCKER=podman if needed)."; }

# ---------------------------------------------------------------------------
# Discovery
# ---------------------------------------------------------------------------

# Print "CONTAINER_ID\tNAME\tWOLF_SESSION_ID" for every running container that
# looks Wolf-managed (has a WOLF_SESSION_ID env var). The Wolf server container
# itself has no session id; include it too, tagged "server", since it is where
# the virtual devices are actually created.
discover_containers() {
  local id name env sid role
  while read -r id; do
    [ -n "$id" ] || continue
    name=$("$DOCKER" inspect -f '{{.Name}}' "$id" 2>/dev/null | sed 's#^/##')
    env=$("$DOCKER" inspect -f '{{range .Config.Env}}{{println .}}{{end}}' "$id" 2>/dev/null || true)
    sid=$(printf '%s\n' "$env" | sed -n 's/^WOLF_SESSION_ID=//p' | head -n1)
    if printf '%s\n' "$env" | grep -q '^WOLF_'; then
      role="session"; [ -n "$sid" ] || { sid="-"; role="server"; }
      printf '%s\t%s\t%s\t%s\n' "$id" "$name" "$sid" "$role"
    fi
  done < <("$DOCKER" ps -q)
}

# List input device nodes inside a container as "PATH MAJOR:MINOR".
# Parses `ls -l` so it works under busybox (no stat/--printf assumptions).
nodes_in_container() {
  local id="$1"
  "$DOCKER" exec "$id" sh -c '
    for n in /dev/input/event* /dev/input/js* /dev/hidraw*; do
      [ -c "$n" ] || continue
      set -- $(ls -l "$n")
      maj=$5; min=$6; maj=${maj%,}
      printf "%s %s:%s\n" "$n" "$maj" "$min"
    done 2>/dev/null' 2>/dev/null || true
}

# Resolve a major:minor to a human name using the HOST kernel view.
# evdev -> /sys/class/input/eventN/{dev,device/name}
# hidraw -> /sys/class/hidraw/hidrawN/{dev, device/uevent:HID_NAME}
host_name_for() {
  local target="$1" d dev
  for d in /sys/class/input/event*; do
    [ -r "$d/dev" ] || continue
    read -r dev < "$d/dev"
    if [ "$dev" = "$target" ]; then
      [ -r "$d/device/name" ] && { cat "$d/device/name"; return; }
    fi
  done
  for d in /sys/class/hidraw/hidraw*; do
    [ -r "$d/dev" ] || continue
    read -r dev < "$d/dev"
    if [ "$dev" = "$target" ]; then
      sed -n 's/^HID_NAME=//p' "$d/device/uevent" 2>/dev/null && return
    fi
  done
  echo "(unknown)"
}

# ---------------------------------------------------------------------------
# HOST-LEAK audit (the wolf#451 class): can the desktop user reach a Wolf pad?
# ---------------------------------------------------------------------------

# List every host input/hidraw device that Wolf created (name starts with "Wolf").
# Emits: NODE<TAB>MAJ:MIN<TAB>NAME<TAB>IS_TOUCHPAD(0/1)
host_wolf_devices() {
  local d dev name tp node
  for d in /sys/class/input/event* /sys/class/hidraw/hidraw*; do
    [ -r "$d/dev" ] || continue
    name=""
    if [ -r "$d/device/name" ]; then name=$(cat "$d/device/name"); fi
    if [ -z "$name" ] && [ -r "$d/device/uevent" ]; then
      name=$(sed -n 's/^HID_NAME=//p' "$d/device/uevent")
    fi
    case "$name" in Wolf*) ;; *) continue ;; esac
    read -r dev < "$d/dev"
    case "$d" in
      */class/input/*)  node="/dev/input/$(basename "$d")" ;;
      */class/hidraw/*) node="/dev/$(basename "$d")" ;;
      *) continue ;;
    esac
    tp=0
    if command -v udevadm >/dev/null 2>&1; then
      udevadm info --query=property --name="$node" 2>/dev/null | grep -q '^ID_INPUT_TOUCHPAD=1' && tp=1
    fi
    printf '%s\t%s\t%s\t%s\n' "$node" "$dev" "$name" "$tp"
  done
}

# Is this device reachable by a non-root user? Returns the offending grantees, or "".
node_nonroot_access() {
  local node="$1" out=""
  # (1) named ACL entries = logind uaccess granted the active-seat desktop user
  if command -v getfacl >/dev/null 2>&1; then
    out=$(getfacl -pE "$node" 2>/dev/null \
          | grep -E '^(user|group):[a-zA-Z0-9]' \
          | grep -vE ':[[:space:]]*---$' || true)
  fi
  # (2) world (other) read/write bit set — chars 7-9 of e.g. "crw-rw----"
  local operm; operm=$(stat -c '%A' "$node" 2>/dev/null || echo '----------')
  local other="${operm:7:3}"
  if [ -n "$other" ] && [ "${other//-/}" != "" ]; then
    out="${out}${out:+; }other=${other}"
  fi
  printf '%s' "$out"
}

# Does 85-wolf.rules contain a rule matching this exact device name?
rule_matches_name() {
  local name="$1"
  [ -r "$RULES_FILE" ] || return 2
  # Pull every ATTR{name}/ATTRS{name} match pattern out of the rules file and
  # test each against the device name as a shell glob. This handles both exact
  # names and globs like "Wolf *virtual*", so coverage stays correct regardless
  # of how the rules are written.
  local pat
  while IFS= read -r pat; do
    # shellcheck disable=SC2254
    case "$name" in
      $pat) return 0 ;;
    esac
  done < <(grep -oE 'ATTRS?\{name\}=="[^"]*"' "$RULES_FILE" | sed -E 's/.*=="([^"]*)"/\1/')
  return 1
}

cmd_host() {
  info "== Host-leak audit: are Wolf virtual devices isolated from the host desktop? =="
  echo "   Rules file: $RULES_FILE"
  [ -r "$RULES_FILE" ] || warn "rules file not readable — cannot cross-check name coverage."
  echo
  local any=0 leaks=0 uncovered=0
  while IFS=$'\t' read -r node mm name tp; do
    [ -n "$node" ] || continue
    any=1
    local label="$name"; [ "$tp" = 1 ] && label="$name [TOUCHPAD pointer]"
    local access; access=$(node_nonroot_access "$node")
    local covered="?"; if rule_matches_name "$name"; then covered="yes"; else covered="no"; fi

    if [ -n "$access" ]; then
      leaks=$((leaks+1))
      bad "LEAK  $node ($mm) '$label'"
      printf '          reachable by non-root: %s\n' "$access"
      [ "$covered" = "no" ] && printf '          → no rule in %s matches name \"%s\"\n' "$RULES_FILE" "$name"
    elif [ "$covered" = "no" ]; then
      uncovered=$((uncovered+1))
      warn "UNCOVERED $node ($mm) '$label' — locked now, but no rule matches \"$name\" (fragile)."
    else
      ok "$node ($mm) '$label' — root-only, rule-covered."
    fi
  done < <(host_wolf_devices)

  echo
  if [ "$any" = 0 ]; then
    warn "No 'Wolf …' virtual devices found on the host. Connect a client + controller first."
    return 0
  fi
  if [ "$leaks" -gt 0 ]; then
    bad "$leaks device(s) readable by the host desktop user — controller input is LEAKING to the host."
    echo  "     Fix: (re)install the shipped 85-wolf.rules into $RULES_FILE — its \"Wolf *virtual*\""
    echo  "     glob covers every name above (pads, the DualSense touchpad/motion child nodes, and hidraw),"
    echo  "     then: sudo udevadm control --reload-rules && sudo udevadm trigger, and re-plug the controller."
    return 1
  elif [ "$uncovered" -gt 0 ]; then
    warn "$uncovered device(s) are locked but not explicitly rule-covered — update 85-wolf.rules to be safe."
  else
    ok "All Wolf devices are root-only and rule-covered. No host leak."
  fi
}

# ---------------------------------------------------------------------------
# Container isolation commands
# ---------------------------------------------------------------------------

cmd_discover() {
  need_docker
  info "== Wolf containers and their input devices =="
  local any=0
  while IFS=$'\t' read -r id name sid role; do
    any=1
    printf '\n\033[1m%s\033[0m  (%s, session=%s)\n' "$name" "$role" "$sid"
    local nodes; nodes=$(nodes_in_container "$id")
    if [ -z "$nodes" ]; then
      warn "no /dev/input or /dev/hidraw nodes present"
      continue
    fi
    while read -r path mm; do
      [ -n "$path" ] || continue
      printf '    %-22s %-8s %s\n' "$path" "$mm" "$(host_name_for "$mm")"
    done <<< "$nodes"
  done < <(discover_containers)
  [ "$any" = 1 ] || warn "No Wolf-managed containers found (are any sessions running?)."
}

# STATIC isolation check (A): fail if any major:minor exists in >1 container.
cmd_static() {
  need_docker
  info "== Static isolation check: is any device node shared across containers? =="
  declare -A owners   # "maj:min" -> "name1|name2|..."
  declare -A dname    # "maj:min" -> device name
  while IFS=$'\t' read -r id name sid role; do
    while read -r path mm; do
      [ -n "$mm" ] || continue
      owners["$mm"]="${owners[$mm]:+${owners[$mm]}|}$name"
      dname["$mm"]="$(host_name_for "$mm")"
    done < <(nodes_in_container "$id")
  done < <(discover_containers)

  local leaks=0 total=0
  for mm in "${!owners[@]}"; do
    total=$((total+1))
    local list="${owners[$mm]}"
    if [[ "$list" == *"|"* ]]; then
      leaks=$((leaks+1))
      bad "LEAK: $mm '${dname[$mm]}' present in multiple containers: ${list//|/, }"
    fi
  done
  echo
  if [ "$total" = 0 ]; then
    warn "No input nodes found in any container."
  elif [ "$leaks" = 0 ]; then
    ok "$total device node(s) checked — each belongs to exactly one container. Isolation intact."
  else
    bad "$leaks shared node(s) detected — input CAN cross containers. See above."
    return 1
  fi
}

# Read one container's event nodes for DUR seconds; echo which nodes saw traffic.
# Non-blocking on our side, non-destructive (no grab). Prints "PATH" per active node.
_watch_container() {
  local id="$1" dur="$2"
  "$DOCKER" exec "$id" sh -c '
    dur='"$dur"'
    for n in /dev/input/event*; do
      [ -c "$n" ] || continue
      ( if timeout "$dur" sh -c "cat \"$n\" | head -c1 >/dev/null 2>&1"; then echo "$n"; fi ) &
    done
    wait
  ' 2>/dev/null || true
}

# MONITOR (A, exploratory): watch ALL containers at once. Operator presses pads;
# each container that sees activity is printed. Anything unexpected = a leak.
cmd_monitor() {
  need_docker
  local dur="${1:-$DUR_DEFAULT}"
  info "== Live monitor: press buttons on your controller(s) now (${dur}s window) =="
  echo "   Each container that registers input will be listed. A container you are"
  echo "   NOT actively using showing up here means input is leaking into it."
  echo
  local tmp; tmp=$(mktemp -d)
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp'" RETURN
  while IFS=$'\t' read -r id name sid role; do
    ( active=$(_watch_container "$id" "$dur")
      [ -n "$active" ] && printf '%s\t%s\n' "$name" "$(echo "$active" | tr '\n' ' ')" >> "$tmp/hits"
    ) &
  done < <(discover_containers)
  wait
  echo
  if [ -s "$tmp/hits" ]; then
    info "Containers that saw input:"
    while IFS=$'\t' read -r name nodes; do printf '  %-24s %s\n' "$name" "$nodes"; done < "$tmp/hits"
  else
    warn "No input seen anywhere. Did you press a button? Is a client connected?"
  fi
}

# LIVE targeted test (A+B): name the container you are ACTIVELY using; press its
# pad; we confirm it received input (B) and that no OTHER container did (A).
cmd_live() {
  need_docker
  local target="${1:?usage: live <container-name> [secs]}"; local dur="${2:-$DUR_DEFAULT}"
  # verify target exists
  discover_containers | cut -f2 | grep -qx "$target" || die "'$target' is not a running Wolf container (see: $0 discover)."
  info "== Targeted test against '$target' =="
  echo "   Actuate the controller for THIS session for ${dur}s now..."
  echo
  local tmp; tmp=$(mktemp -d); trap "rm -rf '$tmp'" RETURN
  local target_id=""
  while IFS=$'\t' read -r id name sid role; do
    [ "$name" = "$target" ] && target_id="$id"
    ( active=$(_watch_container "$id" "$dur")
      [ -n "$active" ] && printf '%s\n' "$name" >> "$tmp/hits"
    ) &
  done < <(discover_containers)
  wait

  echo
  local hit_target=0 leaked=""
  if [ -f "$tmp/hits" ]; then
    grep -qx "$target" "$tmp/hits" && hit_target=1
    leaked=$(grep -vx "$target" "$tmp/hits" | sort -u | tr '\n' ' ')
  fi

  # (B) delivery
  if [ "$hit_target" = 1 ]; then ok "(B) DELIVERY: '$target' received controller input."
  else bad "(B) DELIVERY: '$target' saw NO input — passthrough is broken for this container."; fi
  # (A) isolation
  if [ -z "$leaked" ]; then ok "(A) ISOLATION: no other container saw this controller's input."
  else bad "(A) ISOLATION: input LEAKED into: $leaked"; fi

  [ "$hit_target" = 1 ] && [ -z "$leaked" ]
}

cmd_all() {
  cmd_host || true; echo
  cmd_discover; echo
  cmd_static || true; echo
  info "Next: run a live test while a client is connected and pressing a pad:"
  echo "   $0 monitor            # eyeball which containers light up"
  echo "   $0 live <container>   # pass/fail A+B for the container you're using"
}

case "${1:-all}" in
  host)     cmd_host ;;
  discover) cmd_discover ;;
  static)   cmd_static ;;
  monitor)  shift; cmd_monitor "$@" ;;
  live)     shift; cmd_live "$@" ;;
  all)      cmd_all ;;
  *) die "unknown command '$1' (host|discover|static|monitor|live|all)" ;;
esac
