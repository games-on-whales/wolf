#!/usr/bin/env bash
# Smoke-test a built Wolf image.
#
# Usage: bin/test-image.sh <image-tag> [--keep-logs]
#
# Runs three layers of validation against the already-built image:
#
#   1. Structural  -- docker inspect: image present, entrypoint set,
#                     image.source label set, Moonlight ports exposed.
#   2. Init smoke  -- `docker run --rm <tag> <sentinel>` exercises every
#                     /etc/cont-init.d/*.sh script inherited from the GOW
#                     base image as root, then exits. Catches regressions
#                     in user/device/nvidia setup before they bite a real
#                     client session.
#   3. Image smoke -- runs docker/tests/smoke.sh inside the container
#                     (bind-mounted at /smoke), with a 180s timeout. This
#                     is where wolf-specific assertions live (wolf +
#                     fake-udev binaries present + ldd-clean, custom
#                     gst-plugin-waylanddisplay loads, shipped startup
#                     script present, key env vars set).
#
# The harness is single-image because wolf is a single-image project; no
# --docker-path / --variant flags like in games-on-whales/gow.

set -euo pipefail

usage() {
  cat >&2 <<EOF
Usage: $0 <image-tag> [--keep-logs]

Examples:
  $0 ghcr.io/games-on-whales/wolf:edge
  $0 ghcr.io/games-on-whales/wolf:sha-$(git rev-parse --short HEAD 2>/dev/null || echo abcdef1)
EOF
  exit 64
}

[[ $# -ge 1 ]] || usage
IMAGE_TAG="$1"
shift

KEEP_LOGS=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --keep-logs) KEEP_LOGS=1; shift;;
    -h|--help)   usage;;
    *) echo "!! unknown argument: $1" >&2; usage;;
  esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SMOKE_DIR="$REPO_ROOT/docker/tests"
COMMON_DIR="$REPO_ROOT/bin/tests"

# ---- output helpers -------------------------------------------------------
TOTAL=0; FAIL=0
step() { printf '\n==> %s\n' "$*"; }
pass() { TOTAL=$((TOTAL+1)); printf '   ok   %s\n' "$*"; }
fail() { TOTAL=$((TOTAL+1)); FAIL=$((FAIL+1)); printf '   FAIL %s\n' "$*" >&2; }
dump() { sed 's/^/   | /' "$1" >&2; }

INIT_LOG=$(mktemp)
SMOKE_LOG=$(mktemp)
cleanup() {
  if [[ -n "$KEEP_LOGS" ]]; then
    echo "   (logs kept: $INIT_LOG $SMOKE_LOG)" >&2
  else
    rm -f "$INIT_LOG" "$SMOKE_LOG"
  fi
}
trap cleanup EXIT

# ---- layer 1: structural --------------------------------------------------
step "Layer 1 -- structural checks ($IMAGE_TAG)"

if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
  fail "image present ($IMAGE_TAG)"
  exit 1
fi
pass "image present ($IMAGE_TAG)"

EP=$(docker image inspect --format '{{json .Config.Entrypoint}}' "$IMAGE_TAG")
if [[ "$EP" == *"/entrypoint.sh"* ]]; then
  pass "entrypoint is /entrypoint.sh ($EP)"
else
  fail "entrypoint is /entrypoint.sh (got $EP)"
fi

SRC_LABEL=$(docker image inspect --format '{{index .Config.Labels "org.opencontainers.image.source"}}' "$IMAGE_TAG" 2>/dev/null || true)
if [[ -n "$SRC_LABEL" ]]; then
  pass "has org.opencontainers.image.source label ($SRC_LABEL)"
else
  fail "has org.opencontainers.image.source label"
fi

# Moonlight wire protocol: clients assume these ports. Losing an EXPOSE
# doesn't break docker itself, but it breaks compose files + auto-detection
# tooling that reads the exposed-ports set, so treat it as a regression.
EXPECTED_PORTS=(47984/tcp 47989/tcp 47999/udp 48010/tcp 48100/udp 48200/udp)
EXPOSED=$(docker image inspect --format '{{range $p, $_ := .Config.ExposedPorts}}{{$p}} {{end}}' "$IMAGE_TAG" 2>/dev/null || true)
for p in "${EXPECTED_PORTS[@]}"; do
  if [[ " $EXPOSED " == *" $p "* ]]; then
    pass "exposes $p"
  else
    fail "exposes $p (got: $EXPOSED)"
  fi
done

# ---- layer 2: init smoke --------------------------------------------------
# Wolf inherits its entrypoint from the GOW base image. That entrypoint
# sources /etc/cont-init.d/*.sh as root when $(id -u) is 0 and then, if
# positional args were given, runs `bash -c "$@"` instead of the usual
# startup script. Pass a sentinel echo so every init script executes and
# the container exits 0.
step "Layer 2 -- init smoke (/etc/cont-init.d scripts run clean)"

SENTINEL="__wolf_init_ok_$$__"
if timeout 60 docker run --rm \
      --name "test-wolf-init-$$" \
      "$IMAGE_TAG" \
      "echo $SENTINEL" \
      >"$INIT_LOG" 2>&1; then
  if grep -q "$SENTINEL" "$INIT_LOG"; then
    pass "cont-init.d scripts exit clean"
  else
    fail "cont-init.d scripts exit clean (sentinel missing; see log)"
    dump "$INIT_LOG"
  fi
else
  fail "cont-init.d scripts exit clean (run failed, see log)"
  dump "$INIT_LOG"
fi

# Even with a zero exit, catch latent dynamic-linker / missing-binary chatter.
FATAL_PATTERNS='error while loading shared libraries|cannot open shared object|segmentation fault|command not found'
if grep -Eiq "$FATAL_PATTERNS" "$INIT_LOG"; then
  fail "cont-init.d output is free of fatal-looking errors"
  grep -Eni "$FATAL_PATTERNS" "$INIT_LOG" | sed 's/^/   | /' >&2
else
  pass "cont-init.d output is free of fatal-looking errors"
fi

# ---- layer 3: image smoke ------------------------------------------------
SMOKE="$SMOKE_DIR/smoke.sh"
if [[ -f "$SMOKE" ]]; then
  step "Layer 3 -- image smoke (docker/tests/smoke.sh)"

  # smoke.sh and the shared lib.sh are bind-mounted read-only; image stays
  # untouched. The entrypoint still runs cont-init.d before handing control
  # to the script, so smoke assertions see the same runtime state as a
  # production container.
  if timeout 180 docker run --rm \
        --name "test-wolf-smoke-$$" \
        -v "$COMMON_DIR:/smoke-common:ro" \
        -v "$SMOKE_DIR:/smoke:ro" \
        "$IMAGE_TAG" \
        "/smoke/smoke.sh" \
        >"$SMOKE_LOG" 2>&1; then
    pass "image smoke exits clean"
    dump "$SMOKE_LOG"
  else
    rc=$?
    fail "image smoke exits clean (exit $rc)"
    dump "$SMOKE_LOG"
  fi
else
  step "Layer 3 -- SKIPPED (no docker/tests/smoke.sh)"
fi

# ---- result --------------------------------------------------------------
echo
if [[ "$FAIL" -eq 0 ]]; then
  printf '== PASS == %d/%d checks passed for wolf (%s)\n' "$TOTAL" "$TOTAL" "$IMAGE_TAG"
  exit 0
else
  printf '== FAIL == %d/%d checks failed for wolf (%s)\n' "$FAIL" "$TOTAL" "$IMAGE_TAG" >&2
  exit 1
fi
