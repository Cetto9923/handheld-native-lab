#!/bin/bash
# One-shot pipeline after the game restarts with the ubtrace build:
#   1. arm the uniform fingerprint trace
#   2. capture presented frames so each frame can be labelled SHARP/BLUR
#   3. pull both, then join them by present number
set -u
DEV=RG477M01445122
D=/sdcard/Android/data/org.ffrs.gamewindow/files
OUT=/tmp/ffrs-flicker-csv/ub
mkdir -p "$OUT"
adb -s $DEV shell "run-as org.ffrs.gamewindow rm -f $D/frame-dump/*.bin" 2>/dev/null
# 400 presented frames of uniform fingerprints (the trace stops itself)
adb -s $DEV shell "printf '%s' '400' > $D/gfx-live-ubtrace"
sleep 2
# capture presented frames in contiguous runs of 9
for i in $(seq 1 6); do
  adb -s $DEV shell "printf '%s' '9' > $D/gfx-live-framedump"
  sleep 1.6
done
sleep 1
rm -rf "$OUT/frames"; mkdir -p "$OUT/frames"
adb -s $DEV pull "$D/frame-dump" "$OUT/frames" >/dev/null 2>&1
adb -s $DEV pull "$D"/ubtrace-*.csv "$OUT/" >/dev/null 2>&1
echo "frames: $(ls "$OUT/frames/frame-dump" 2>/dev/null | grep -c '\.bin$')"
ls -la "$OUT"/ubtrace-*.csv 2>/dev/null
adb -s $DEV shell "grep -a 'LIVE-UBTRACE' $D/engine.log | tail -3" | tr -d '\r'
echo UB_PIPELINE_DONE
