#!/usr/bin/env bash
# Wolf entrypoint, in front of the GOW base /entrypoint.sh.
#
# Without flags it hands straight over to /entrypoint.sh (root-only init
# scripts, then gosu to $UNAME), exactly as before. That is also the way to
# start as root and drop privileges: set UNAME/PUID/PGID.
#
# With --rootless (minimal image only) the container must already be started
# as a non-root user (e.g. `--user wolf`, or `User=wolf` in a quadlet). The GOW
# init and gosu, which both need root, are skipped and Wolf runs as that user.
# Device access comes from the groups the runtime grants
# (e.g. `--group-add keep-groups`).
set -e

source /opt/gow/bash-lib/utils.sh

if [ "${1:-}" != "--rootless" ]; then
    # The image ships a `wolf` system user for --rootless. If the GOW init is
    # asked to create a user clashing with it (UNAME=wolf, or its UID/GID),
    # remove ours first so 10-setup_user.sh can create it with the requested IDs.
    wolf_uid=$(id -u wolf 2>/dev/null || true)
    wolf_gid=$(id -g wolf 2>/dev/null || true)
    if [ "$(id -u)" = "0" ] && [ -n "$wolf_uid" ] && [ "${UNAME:-root}" != "root" ] &&
        { [ "$UNAME" = "wolf" ] || [ "${PUID:-1000}" = "$wolf_uid" ] || [ "${PGID:-1000}" = "$wolf_gid" ]; }; then
        gow_log "UNAME/PUID/PGID clash with the built-in wolf user, removing it"
        userdel wolf 2>/dev/null || true
        groupdel wolf 2>/dev/null || true
    fi
    exec /entrypoint.sh "$@"
fi

if [ -f /etc/supervisord.conf ]; then
    gow_log "--rootless is only supported by the -minimal image (supervisord here runs as root)"
    exit 1
fi

if [ "$(id -u)" = "0" ]; then
    gow_log "--rootless needs the container started as a non-root user (e.g. --user wolf)."
    gow_log "To start as root and drop privileges instead, drop --rootless and set UNAME/PUID/PGID."
    exit 1
fi

# The base image's HOME (/home/retro) isn't writable here, keep caches (e.g.
# the GStreamer registry) in the state folder instead
export HOME=$HOST_APPS_STATE_FOLDER

gow_log "Running Wolf rootless as $(id -un 2>/dev/null || id -u)"
exec /opt/gow/startup.sh
