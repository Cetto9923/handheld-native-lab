# -*- coding: utf-8 -*-
"""Flicker detection over contiguous frame runs.

Each armed capture yields a run of consecutive presents. Inside a run the
scene is static, so:
  - the ROI mean brightness takes one of two levels (veil on / veil off)
  - the veil alpha can be measured exactly from the pair
We report per run: whether the run contains both states, the number of
transitions, and the measured alpha. A level that removes the flicker shows
runs with zero transitions and no bimodal split.
"""
import numpy as np, os, glob, sys, collections

ROOT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ffrs-flicker-csv/ab5"
ROI = (260, 560, 400, 540)

def load(p):
    raw = open(p, "rb").read()
    i = raw.index(b"\n"); hdr = raw[:i].decode("latin1"); body = raw[i+1:]
    d = dict(kv.split("=", 1) for kv in hdr.split() if "=" in kv)
    w = int(d["width"]); h = int(d["height"]); bpp = int(d["bpp"])
    return hdr, np.frombuffer(body[:w*h*bpp], dtype=np.uint8).reshape(h, w, bpp)

def lum(a):
    return (a[:, :, :3].astype(np.float32) * np.array([0.299,0.587,0.114], dtype=np.float32)).sum(2)

summary = {}
for level in sorted(os.listdir(ROOT)):
    d = os.path.join(ROOT, level, "frame-dump")
    files = sorted(glob.glob(os.path.join(d, "*.bin")))
    recs = {}
    for p in files:
        hdr, a = load(p)
        present = int(hdr.split("present=")[1].split()[0])
        l = lum(a)
        recs[present] = (l, float(l[ROI[2]:ROI[3], ROI[0]:ROI[1]].mean()))
    ks = sorted(recs)
    runs = []
    cur = [ks[0]]
    for k in ks[1:]:
        if k == cur[-1] + 1: cur.append(k)
        else: runs.append(cur); cur = [k]
    runs.append(cur)
    runs = [r for r in runs if len(r) >= 4]

    n_trans = 0; alphas = []; bimodal_runs = 0; run_gaps = []
    for r in runs:
        means = np.array([recs[k][1] for k in r])
        gaps = np.diff(means)
        big = int(np.sum(np.abs(gaps) >= 3.0))
        n_trans += big
        if big > 0: bimodal_runs += 1
        run_gaps.append(float(np.max(np.abs(gaps))) if len(gaps) else 0.0)
        # alpha of each transition
        for i in range(len(r)-1):
            if abs(gaps[i]) < 3.0: continue
            x = recs[r[i]][0][ROI[2]:ROI[3], ROI[0]:ROI[1]].ravel()
            y = recs[r[i+1]][0][ROI[2]:ROI[3], ROI[0]:ROI[1]].ravel()
            w = np.maximum(255.0-x, 1.0)
            alphas.append(float(np.sum(w*(y-x))/np.sum(w*w)))
    print("=== %s : %d frames in %d runs ===" % (level, len(ks), len(runs)))
    print("  runs with a state switch: %d/%d   transitions=%d   maxJump=%.2f"
          % (bimodal_runs, len(runs), n_trans, max(run_gaps) if run_gaps else 0))
    if alphas:
        al = np.array(alphas)
        print("  veil alpha over %d transitions: mean=%+.4f std=%.4f min=%+.4f max=%+.4f"
              % (len(al), al.mean(), al.std(), al.min(), al.max()))
    else:
        print("  no state switch detected in any run")
    summary[level] = (len(runs), bimodal_runs, n_trans, float(np.mean(alphas)) if alphas else 0.0)

print("\n=== summary ===")
print("%-14s %6s %10s %8s %10s" % ("level", "runs", "runsWithFlick", "trans", "meanAlpha"))
for level, (nr, bf, nt, ma) in summary.items():
    print("%-14s %6d %10d %8d %10.4f" % (level, nr, bf, nt, ma))
