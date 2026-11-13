#!/bin/bash
# Capture contiguous frame runs: one armed marker yields 9 consecutive presents.
# Each run is analysed on its own, so a missing frame between runs never
# corrupts a within-run comparison.
set -u
DEV=RG477M01445122
D=/sdcard/Android/data/org.ffrs.gamewindow/files
OUT=/tmp/ffrs-flicker-csv/ab5
RUNS=10
mkdir -p "$OUT"
for p in "$@"; do
  if [ "$p" = baseline ]; then
    adb -s $DEV shell "printf '%s' 'none' > $D/gfx-live-ab"
  else
    adb -s $DEV shell "printf '%s' '$p' > $D/gfx-live-ab"
  fi
  sleep 2.0
  adb -s $DEV shell "run-as org.ffrs.gamewindow rm -f $D/frame-dump/*.bin" 2>/dev/null
  for i in $(seq 1 "$RUNS"); do
    adb -s $DEV shell "printf '%s' '9' > $D/gfx-live-framedump"
    sleep 1.6
  done
  sleep 1.0
  rm -rf "$OUT/$p"; mkdir -p "$OUT/$p"
  adb -s $DEV pull "$D/frame-dump" "$OUT/$p" >/dev/null 2>&1
  n=$(ls "$OUT/$p/frame-dump" 2>/dev/null | grep -c "\.bin$")
  contig=$(ls "$OUT/$p/frame-dump" 2>/dev/null | sed "s/^f//;s/\.bin$//" | sort -n | awk 'NR==1{p=$1;c=1;next}{if($1==p+1){p=$1;c++}else{if(c>m)m=c;c=1;p=$1}}END{if(c>m)m=c;print m}')
  perf=$(adb -s $DEV shell "grep -a 'PERF v=1' $D/engine.log | tail -1" | tr -d "\r" | grep -o "fps=[0-9.]*" | head -1)
  echo "level=$p frames=$n maxContiguousRun=$contig $perf"
done
adb -s $DEV shell "printf '%s' 'none' > $D/gfx-live-ab"
echo COLLECT_DONE
