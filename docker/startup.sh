#!/bin/bash
set -e

# Update fake-udev if missing from the path
cp /wolf/fake-udev $WOLF_CFG_FOLDER/fake-udev

exec /wolf/wolf