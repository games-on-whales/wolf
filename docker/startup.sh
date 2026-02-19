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

# Copy fake-uinput broker and LD_PRELOAD library for Steam Input support
export WOLF_DOCKER_FAKE_UINPUT_BROKER_PATH=${WOLF_DOCKER_FAKE_UINPUT_BROKER_PATH:-$HOST_APPS_STATE_FOLDER/fake-uinput-broker}
cp /wolf/fake-uinput-broker $WOLF_DOCKER_FAKE_UINPUT_BROKER_PATH
if [ -f /wolf/libfake-uinput.so ]; then
    cp /wolf/libfake-uinput.so $HOST_APPS_STATE_FOLDER/libfake-uinput.so
fi
if [ -f /wolf/libfake-uinput-32.so ]; then
    cp /wolf/libfake-uinput-32.so $HOST_APPS_STATE_FOLDER/libfake-uinput-32.so
fi

exec /wolf/wolf