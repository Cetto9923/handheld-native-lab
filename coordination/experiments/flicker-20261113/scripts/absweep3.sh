#!/bin/bash
# Scene-guarded A/B over the engine's own ab_off paths, judged on real ground pixels.
# Each level: force the path off, capture consecutive presented frames, pull them.
set -u
DEV=RG477M01445122
D=/sdcard/Android/data/org.ffrs.gamewindow/files
OUT=/tmp/ffrs-flicker-csv/ab3
mkdir -p "$OUT"
PATHS="baseline memofront descreserve ktag hitgen texdirty uppro occ bright poolreset wtrack"
for p in $PATHS; do
  if [ "$p" = baseline ]; then
    adb -s $DEV shell "printf '%s' 'none' > $D/gfx-live-ab"
  else
    adb -s $DEV shell "printf '%s' '$p' > $D/gfx-live-ab"
  fi
  sleep 1.5
  adb -s $DEV shell "run-as org.ffrs.gamewindow rm -f $D/frame-dump/*.bin" 2>/dev/null
  for i in 1 2 3; do
    adb -s $DEV shell "printf '%s' '9' > $D/gfx-live-framedump"
    sleep 0.7
  done
  sleep 1.2
  rm -rf "$OUT/$p"
  mkdir -p "$OUT/$p"
  adb -s $DEV pull "$D/frame-dump" "$OUT/$p" >/dev/null 2>&1
  n=$(ls "$OUT/$p/frame-dump" 2>/dev/null | grep -c '\.bin$')
  perf=$(adb -s $DEV shell "grep -a 'PERF v=1' $D/engine.log | tail -1" | tr -d '\r' | grep -o 'draws=[0-9.]*' | head -1)
  echo "level=$p frames=$n $perf"
done
adb -s $DEV shell "printf '%s' 'none' > $D/gfx-live-ab"
echo "SWEEP_DONE"
