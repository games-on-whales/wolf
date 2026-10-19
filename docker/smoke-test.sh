#!/bin/bash
# Smoke test for the Wolf image. Run inside the built image, e.g.:
#   docker run --rm --entrypoint "" -v "$(pwd)/docker/smoke-test.sh:/smoke.sh:ro" \
#     ghcr.io/games-on-whales/wolf:<tag> bash /smoke.sh
#
# Only checks things that do NOT depend on GPU hardware: HW encoder
# elements (nvh264enc, vaapih264enc, ...) register lazily and need a
# driver, so we assert their plugin *files* exist instead.
set -euo pipefail

fail() { echo "SMOKE TEST FAILED: $*" >&2; exit 1; }

echo "==> 1. Binaries present and executable"
for bin in /wolf/wolf /wolf/fake-udev; do
  [ -x "$bin" ] || fail "$bin missing or not executable"
done

echo "==> 2. All dynamic dependencies resolve"
if ldd /wolf/wolf 2>&1 | grep -q "not found"; then
  ldd /wolf/wolf | grep "not found"
  fail "wolf has unresolved libraries"
fi
if ldd /wolf/fake-udev 2>&1 | grep -q "not found"; then
  ldd /wolf/fake-udev | grep "not found"
  fail "fake-udev has unresolved libraries"
fi

echo "==> 3. Custom compositor plugin resolves against our GStreamer"
PLUGIN_SO=$(ls /usr/local/lib/x86_64-linux-gnu/gstreamer-1.0/libgstwaylanddisplaysrc.so)
if ldd "$PLUGIN_SO" 2>&1 | grep -q "not found"; then
  ldd "$PLUGIN_SO" | grep "not found"
  fail "waylanddisplay plugin has unresolved libraries"
fi

echo "==> 4. waylanddisplaysrc element registered"
gst-inspect-1.0 waylanddisplaysrc >/dev/null 2>&1 || fail "waylanddisplaysrc element not found"

echo "==> 5. Software encoder present"
gst-inspect-1.0 x264enc >/dev/null 2>&1 || fail "x264enc element not found"

echo "==> 6. HW encoder plugin files present (elements need a GPU to register)"
GST_PLUGINS_DIR=/usr/local/lib64/gstreamer-1.0
for plugin in libgstnvcodec.so libgstvaapi.so libgstmsdk.so; do
  [ -e "$GST_PLUGINS_DIR/$plugin" ] || fail "$plugin missing from $GST_PLUGINS_DIR"
done

echo "==> 7. VA-API driver files present (encoding still needs a GPU)"
[ -e /usr/lib64/dri/iHD_drv_video.so ] || fail "Intel iHD VA driver missing (libva-intel-media-driver not installed?)"
[ -e /usr/lib64/dri-freeworld/radeonsi_drv_video.so ] || fail "AMD freeworld VA driver missing (mesa-va-drivers-freeworld not installed?)"

echo "==> 8. Embedded PulseAudio + supervisord present"
command -v supervisord >/dev/null || fail "supervisord not found"
command -v pulseaudio >/dev/null || fail "pulseaudio not found"
command -v pactl >/dev/null || fail "pactl not found"

echo "==> 9. Boot the real entrypoint (s6 -> startup.sh -> supervisord)"
BOOT_LOG=/tmp/boot.log
# No docker socket in the test container: wolf logs warnings but must stay up
set +e
timeout 30 /entrypoint.sh >"$BOOT_LOG" 2>&1
rc=$?
set -e
# 124 = still running when the timeout hit, which is what we want
[ "$rc" -eq 124 ] || { tail -50 "$BOOT_LOG"; fail "container exited early (rc=$rc)"; }
grep -q "pulseaudio entered RUNNING state" "$BOOT_LOG" || { tail -50 "$BOOT_LOG"; fail "PulseAudio did not reach RUNNING state under supervisord"; }
grep -q "wolf entered RUNNING state" "$BOOT_LOG" || { tail -50 "$BOOT_LOG"; fail "wolf did not reach RUNNING state under supervisord"; }
grep -q "HTTPS server listening on port: 47984" "$BOOT_LOG" || { tail -50 "$BOOT_LOG"; fail "HTTPS server did not start"; }

echo "ALL SMOKE TESTS PASSED"
