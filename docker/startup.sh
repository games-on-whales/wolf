#!/bin/bash
set -e

# Update fake-udev if missing from the path
cp /wolf/fake-udev $WOLF_DOCKER_FAKE_UDEV_PATH

exec /wolf/wolf