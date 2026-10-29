#!/bin/bash
# Deploy the FFRS game (main.nso + romfs + modules) from a source device to a target device, via a host staging dir.
# Usage:
#   deploy_game.sh pull  <src_serial>   # device -> host staging (resumable: skips files whose size already matches)
#   deploy_game.sh push  <dst_serial>   # host staging -> device (resumable)
#   deploy_game.sh check <serial>       # compare sizes with the staging manifest
# Staging lives under .local (never committed).  Source run dir is auto-detected from the app log on the source device.
set -u
ROOT=/Users/yuyan9923/GitHub/ff-resonance-rg477m-native
STAGE=$ROOT/.local/deploy-stage/game
SRC_DIR=/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259
DST_DIR=/data/local/tmp/ffrs-game
mode=${1:-}; ser=${2:-}
[ -n "$mode" ] && [ -n "$ser" ] || { echo "usage: $0 pull|push|check <serial>"; exit 2; }
A="adb -s $ser"
mkdir -p "$STAGE"

case "$mode" in
pull)
  # What to take: everything except per-run logs / exit codes.  List top-level first for the record.
  $A shell "ls -1 $SRC_DIR" | tr -d '\r' > "$STAGE/../toplevel.txt"
  echo "top-level entries on source: $(wc -l < "$STAGE/../toplevel.txt")"
  cat "$STAGE/../toplevel.txt" | head -60
  ;;
*) echo "mode $mode not implemented yet"; exit 2 ;;
esac
