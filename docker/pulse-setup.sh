#!/bin/bash

pulseaudio --log-level=1 &

sleep 1
# We have to create a sink in order to be picked up by pulsesrc
pacmd load-module module-null-sink sink_name=MySink
pacmd update-sink-proplist MySink device.description=MySink

# Check that we get a valid source using: pacmd list-sources

wait -n