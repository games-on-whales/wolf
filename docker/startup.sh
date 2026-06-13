#!/bin/bash
set -e

# Make sure configure folder exists
# as Wolf may try to create default config in non existing folder and crash.
# See https://github.com/games-on-whales/wolf/pull/65#discussion_r1509235307
# and https://github.com/games-on-whales/wolf/issues/64#issuecomment-1951479056
export WOLF_CFG_FOLDER=$HOST_APPS_STATE_FOLDER/cfg
mkdir -p $WOLF_CFG_FOLDER
# Adjust env variables if the user moved the folder
export WOLF_CFG_FILE=$WOLF_CFG_FOLDER/config.toml
export WOLF_PRIVATE_KEY_FILE=$WOLF_CFG_FOLDER/key.pem
export WOLF_PRIVATE_CERT_FILE=$WOLF_CFG_FOLDER/cert.pem

# Set default values for environment variables
export WOLF_RENDER_NODE=${WOLF_RENDER_NODE:-/dev/dri/renderD128}
export WOLF_ENCODER_NODE=${WOLF_ENCODER_NODE:-$WOLF_RENDER_NODE}
export GST_GL_DRM_DEVICE=${GST_GL_DRM_DEVICE:-$WOLF_ENCODER_NODE}

# Update fake-udev if missing from the path
export WOLF_DOCKER_FAKE_UDEV_PATH=${WOLF_DOCKER_FAKE_UDEV_PATH:-$HOST_APPS_STATE_FOLDER/fake-udev}
cp /wolf/fake-udev $WOLF_DOCKER_FAKE_UDEV_PATH

# Run PulseAudio and Wolf side by side under supervisord. PulseAudio lives
# inside the Wolf container instead of the legacy "WolfPulseAudio" sidecar:
# supervisord starts PA first, restarts it if it dies, and stops both cleanly
# when the container is stopped. Wolf waits for the PA socket before starting
# (see supervisord.conf), so there's no startup race and no timeout to tune.
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp/sockets}
mkdir -p "$XDG_RUNTIME_DIR"

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