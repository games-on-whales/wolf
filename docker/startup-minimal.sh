#!/bin/bash
set -e

source /opt/gow/startup-common.sh

# Minimal image: no embedded PulseAudio and no supervisord, Wolf is the only
# process in the container. Point PULSE_SERVER at an external server (e.g. a
# separate ghcr.io/games-on-whales/pulseaudio container sharing a socket);
# if it's unset Wolf falls back to spinning up the WolfPulseAudio sidecar.
exec /wolf/wolf
