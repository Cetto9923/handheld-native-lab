#!/usr/bin/env bash
# RG477M 闪烁 A/B：主角左侧地面白色区域（第 3 章水都运河广场）
#
# 用法：  bash scripts/device/flicker_ab.sh <0|1>
#   0 = 关闭深度附件采样复用（gfx-depth-aspect=0）
#   1 = 开启（现状，等价于 auto 在 RG477M 上的设备默认）
#
# 步骤：
#   1. 写标记
#   2. 重启游戏进程（标记只在启动时读一次）
#   3. 等场景就绪
#   4. 录 10 秒屏幕
#   5. 逐像素统计主角左侧地面白色斑块的帧间波动
#
# 判据：白斑像素簇的 "std over time" 从 ~13.3 降到 ~0 -> 深度复用路径是闪烁根因。
#
# 注意：此脚本会重启设备上的游戏进程。运行前先取得用户同意。
set -euo pipefail

DEV="${FFRS_DEV:-RG477M01445122}"
PKG=org.ffrs.gamewindow
ACT=org.ffrs.diagnostic.GameActivity
FILES=/sdcard/Android/data/org.ffrs.gamewindow/files
WORK="${FFRS_WORK:-/tmp/ffrs-flicker-ab}"
MODE="${1:-0}"

mkdir -p "$WORK"
echo "== device: $DEV  mode(gfx-depth-aspect)=$MODE =="

# 1. marker
adb -s "$DEV" shell "printf '%s' '$MODE' > $FILES/gfx-depth-aspect"
echo "-- marker now: '$(adb -s "$DEV" shell "cat $FILES/gfx-depth-aspect")'"

# 2. restart
adb -s "$DEV" shell "am force-stop $PKG" || true
sleep 2
adb -s "$DEV" shell "am start -n $PKG/$ACT"
echo "-- app started, waiting for the scene (the water capital needs the save, ~75s)"

sleep 75

# 3. record
OUT="$WORK/flick_$MODE.mp4"
adb -s "$DEV" shell "rm -f $FILES/flick_ab.mp4" || true
adb -s "$DEV" shell "screenrecord --time-limit 10 --bit-rate 12000000 $FILES/flick_ab.mp4"
adb -s "$DEV" pull "$FILES/flick_ab.mp4" "$OUT"

# 4. analyze
FR="$WORK/frames_$MODE"
mkdir -p "$FR"
rm -f "$FR"/f_*.png
ffmpeg -v error -i "$OUT" -vsync 0 "$FR/f_%04d.png"

python3 - "$FR" << 'PY'
import sys, glob
import numpy as np
from PIL import Image

fr = sorted(glob.glob(sys.argv[1] + "/f_*.png"))
if not fr:
    raise SystemExit("no frames decoded")

def load(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(np.float32)

REG = (430, 650, 230, 520)   # y0,y1,x0,x1 -- the white patch left of the protagonist
y0, y1, x0, x1 = REG
L = np.stack([load(p)[y0:y1, x0:x1].mean(axis=2) for p in fr])
sd = L.std(axis=0)
m = sd >= np.percentile(sd, 99)
traj = L[:, m].mean(axis=1)

print("frames           :", len(fr))
print("cluster px       :", int(m.sum()))
print("cluster std/time : %.2f   <-- the flicker metric" % L[:, m].mean(axis=1).std())
print("trajectory head  :", np.round(traj[:24], 1).tolist())

d = np.abs(np.diff(L.mean(axis=(1, 2))))
print("region MAD med   : %.3f  max %.3f" % (np.median(d), d.max()))
PY

echo "== done: $WORK =="
