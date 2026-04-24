#!/usr/bin/env bash
# Smoke test for the wolf runtime image.
#
# Runs inside the built image after cont-init.d has executed (see
# bin/test-image.sh). Asserts the things a running wolf depends on:
# the two binaries we build (wolf + fake-udev), their shared-lib
# closure, the custom gst-plugin-waylanddisplay actually loading,
# and the runtime helpers the startup script shells out to.

source /smoke-common/lib.sh

# --- wolf + fake-udev binaries ---------------------------------------------
# The two executables the builder stage copies into /wolf.
assert_path /wolf/wolf /wolf/fake-udev

# ldd-clean catches the "build stage installed libfoo-dev, runtime stage
# forgot to install libfoo" class of regression. Wolf links against a lot
# (boost, curl, ssl, evdev, udev, drm, pci, unwind, glib/gl/egl) so this
# is the single highest-value check in the whole harness.
assert_shared_ok /wolf/wolf
assert_shared_ok /wolf/fake-udev

# fake-udev's --help is self-contained (no netlink socket needed), so
# exercising it end-to-end proves the binary is actually usable, not
# just ldd-clean.
assert_version /wolf/fake-udev --help

# --- gstreamer custom compositor -------------------------------------------
# Wolf builds its own gst plugin (gst-wayland-display) and the runtime
# stage copies it into GST_PLUGIN_PATH alongside the companion .so in
# /usr/local/lib. Registration data should be cached on first use.
assert_has gst-inspect-1.0
assert_env GST_PLUGIN_PATH gstreamer-1.0

# The plugin artefacts themselves. shopt -s nullglob so the check
# fails loud-and-clear if the COPY --from=wolf-builder glob ever
# expands to nothing.
shopt -s nullglob
gst_plugins=("$GST_PLUGIN_PATH"/libgstwaylanddisplay*)
companion_libs=(/usr/local/lib/liblibgstwaylanddisplay*)
shopt -u nullglob
if (( ${#gst_plugins[@]} > 0 )); then
  ok "gst-plugin-waylanddisplay present (${gst_plugins[*]})"
else
  bad "gst-plugin-waylanddisplay present in $GST_PLUGIN_PATH"
fi
if (( ${#companion_libs[@]} > 0 )); then
  ok "liblibgstwaylanddisplay present (${companion_libs[*]})"
else
  bad "liblibgstwaylanddisplay present in /usr/local/lib"
fi

# End-to-end plugin load: gst-inspect-1.0 dlopens the plugin and queries
# its element factories. This catches ABI drift between the builder and
# runtime stages' gstreamer (e.g. someone bumps GSTREAMER_VERSION in the
# build-args for one stage but not the other) in a way that pure ldd
# can't -- gstreamer uses its own symbol-version dance on top of ld.so.
if timeout 15 gst-inspect-1.0 waylanddisplay >/dev/null 2>&1; then
  ok "gst-inspect-1.0 waylanddisplay loads the plugin"
else
  bad "gst-inspect-1.0 waylanddisplay loads the plugin (see: gst-inspect-1.0 waylanddisplay)"
fi

# --- runtime helpers wolf shells out to ------------------------------------
# The compositor needs Xwayland; startup.sh assumes a working /wolf
# working directory and /opt/gow/startup-app.sh (the base-app entrypoint
# hands control to this when UNAME is set).
assert_has Xwayland
assert_path /opt/gow/startup-app.sh /wolf

# --- key env vars the startup path assumes --------------------------------
# These are set via ENV in docker/wolf.Dockerfile. A regression here
# (someone unsets WOLF_CFG_FOLDER in a cont-init script, say) would
# make the container crash on first run with a confusing "cannot
# create /cfg" message rather than a clean failure here.
assert_env WOLF_CFG_FOLDER     /etc/wolf
assert_env WOLF_RENDER_NODE    /dev/dri
assert_env GST_GL_API          gles2
assert_env GST_GL_PLATFORM     egl
assert_env HOST_APPS_STATE_FOLDER /etc/wolf
assert_env UNAME

smoke_report
