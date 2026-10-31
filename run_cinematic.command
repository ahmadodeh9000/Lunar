#!/bin/bash
# Records the LunarCraft cinematic: full-screen capture while the autopilot plays.
cd "$(dirname "$0")"
mkdir -p recordings
screencapture -v -V 40 recordings/lunarcraft_raw.mov &
REC=$!
sleep 2
./lunar examples/minecraft/cinematic.lunar
wait $REC
echo "DONE: recordings/lunarcraft_raw.mov"
