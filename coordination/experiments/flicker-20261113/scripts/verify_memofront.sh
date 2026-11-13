#!/bin/bash
# Re-verify the memofront lead with far more frames per level.
set -u
DEV=RG477M01445122
D=/sdcard/Android/data/org.ffrs.gamewindow/files
OUT=/tmp/ffrs-flicker-csv/ab4
mkdir -p "$OUT"
for p in "$@"; do
  if [ "$p" = baseline ]; then
    adb -s $DEV shell "printf '%s' 'none' > $D/gfx-live-ab"
  else
    adb -s $DEV shell "printf '%s' '$p' > $D/gfx-live-ab"
  fi
  sleep 2.0
  adb -s $DEV shell "run-as org.ffrs.gamewindow rm -f $D/frame-dump/*.bin" 2>/dev/null
  # 8 rounds x 9 frames = 72 frames per level
  for i in $(seq 1 8); do
    adb -s $DEV shell "printf '%s' '9' > $D/gfx-live-framedump"
    sleep 0.75
  done
  sleep 1.5
  rm -rf "$OUT/$p"; mkdir -p "$OUT/$p"
  adb -s $DEV pull "$D/frame-dump" "$OUT/$p" >/dev/null 2>&1
  n=$(ls "$OUT/$p/frame-dump" 2>/dev/null | grep -c '\.bin$')
  perf=$(adb -s $DEV shell "grep -a 'PERF v=1' $D/engine.log | tail -1" | tr -d '\r' | grep -o 'fps=[0-9.]*' | head -1)
  echo "level=$p frames=$n $perf"
done
adb -s $DEV shell "printf '%s' 'none' > $D/gfx-live-ab"
echo "VERIFY_DONE"
