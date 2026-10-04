#!/bin/bash
set -e

source /opt/gow/startup-common.sh

# Run PulseAudio and Wolf side by side under supervisord. PulseAudio lives
# inside the Wolf container instead of the legacy "WolfPulseAudio" sidecar:
# supervisord starts PA first, restarts it if it dies, and stops both cleanly
# when the container is stopped. Wolf waits for the PA socket before starting
# (see supervisord.conf), so there's no startup race and no timeout to tune.
if [ -z "${PULSE_SERVER:-}" ] && command -v pulseaudio >/dev/null 2>&1; then
    # Manage our own PulseAudio (the default). Bare path, no "unix:" prefix, so
    # Wolf reports it verbatim to the app containers and the socket mount matches.
    export PULSE_SERVER="$XDG_RUNTIME_DIR/pulse-socket"
    export WOLF_EMBED_PULSE=true
    # Remove a stale socket, PulseAudio refuses to start otherwise
    rm -f "$PULSE_SERVER"
else
    # An external PULSE_SERVER was provided, or pulseaudio isn't installed: don't
    # manage PA ourselves. Wolf connects to PULSE_SERVER if set, otherwise it
    # falls back to spinning up the sidecar container.
    export PULSE_SERVER="${PULSE_SERVER:-$XDG_RUNTIME_DIR/pulse-socket}"
    export WOLF_EMBED_PULSE=false
fi

exec supervisord -c /etc/supervisord.conf
