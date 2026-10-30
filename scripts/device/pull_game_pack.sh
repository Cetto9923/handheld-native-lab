#!/bin/bash
# Pull main.nso + romfs from the RG477M run dir into the release pack (host side). Resumable at file level for main.nso;
# romfs is pulled as one tree, re-run replaces nothing that already matches (verified afterwards by verify step).
set -u
ROOT=/Users/yuyan9923/GitHub/ff-resonance-rg477m-native
PACK=$ROOT/.local/release-pack-20261028
R=RG477M01445122
SRC=/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259
mkdir -p "$PACK/game" "$PACK/apk"
echo "[$(date +%T)] pull main.nso"
adb -s $R pull "$SRC/main.nso" "$PACK/game/main.nso" || echo "main.nso pull FAILED"
echo "[$(date +%T)] pull romfs (10GB)"
adb -s $R pull "$SRC/romfs" "$PACK/game/" || echo "romfs pull FAILED"
echo "[$(date +%T)] pull done"
echo $? > "$PACK/pull.exitcode"
