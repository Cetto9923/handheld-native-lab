# -*- coding: utf-8 -*-
"""Is the veil a per-frame random dither, or a coherent on/off state?

If the veil were a per-frame independent random overlay (e.g. stochastic
alpha / dithered transparency), the reconstructed alpha map would decorrelate
between consecutive BLUR frames. If it is a coherent state that turns on and
off, consecutive BLUR frames should carry the *same* alpha map.

We also test whether the veil strength itself is constant (a fixed blend
factor) or varies frame to frame.
"""
import numpy as np, os, glob, sys, re, collections

FRAME_DIR = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ffrs-flicker-csv/more"
ROI = (260, 560, 400, 540)

def load(p):
    raw = open(p, "rb").read()
    i = raw.index(b"\n"); hdr = raw[:i].decode("latin1"); body = raw[i+1:]
    d = dict(kv.split("=", 1) for kv in hdr.split() if "=" in kv)
    w = int(d["width"]); h = int(d["height"]); bpp = int(d["bpp"])
    return hdr, np.frombuffer(body[:w*h*bpp], dtype=np.uint8).reshape(h, w, bpp)

def lum(a):
    return (a[:, :, :3].astype(np.float32) * np.array([0.299,0.587,0.114], dtype=np.float32)).sum(2)

F = {}
for p in sorted(glob.glob(os.path.join(FRAME_DIR, "*.bin"))):
    hdr, a = load(p)
    F[int(hdr.split("present=")[1].split()[0])] = a
ks = sorted(F)
batches, cur = [], [ks[0]]
for k in ks[1:]:
    if k == cur[-1] + 1: cur.append(k)
    else: batches.append(cur); cur = [k]
batches.append(cur)

state = {}
for b in batches:
    vals = sorted((float(lum(F[k])[ROI[2]:ROI[3], ROI[0]:ROI[1]].mean()), k) for k in b)
    arr = np.array([v for v, _ in vals]); g = np.diff(arr)
    cut = int(np.argmax(g)) if len(g) else -1
    if cut < 0 or g[cut] < 3.0:
        for _, k in vals: state[k] = "SHARP"
    else:
        hi = {k for _, k in vals[cut+1:]}
        for _, k in vals: state[k] = "BLUR" if k in hi else "SHARP"

print("=== alpha strength per BLUR frame (vs nearest SHARP in the same batch) ===")
rows = []
for b in batches:
    bs = [k for k in b if state[k] == "BLUR"]
    ss = [k for k in b if state[k] == "SHARP"]
    if not bs or not ss: continue
    for k in bs:
        ref = min(ss, key=lambda s: abs(s - k))
        A = lum(F[k]).astype(np.float64); B = lum(F[ref]).astype(np.float64)
        with np.errstate(divide="ignore", invalid="ignore"):
            al = (A - B) / np.maximum(255.0 - B, 1.0)
        al = np.nan_to_num(al, nan=0.0, posinf=0.0, neginf=0.0)
        m = al[ROI[2]:ROI[3], ROI[0]:ROI[1]]
        # best-fit alpha (least squares through the veil model) and its R^2
        x = B[ROI[2]:ROI[3], ROI[0]:ROI[1]].ravel(); y = A[ROI[2]:ROI[3], ROI[0]:ROI[1]].ravel()
        w = np.maximum(255.0 - x, 1.0)
        num = float(np.sum(w*(y-x))); den = float(np.sum(w*w))
        alpha = num/den
        pred = x + alpha*w
        ss_res = float(np.sum((y-pred)**2)); ss_tot = float(np.sum((y-y.mean())**2))
        r2 = 1-ss_res/ss_tot if ss_tot>0 else 0
        rows.append((k, ref, alpha, r2, float(m.mean()), float(m.std())))
        print("  blur=%6d ref=%6d  alpha=%.4f  R2=%.4f  alphaMap mean=%+.4f std=%.4f"
              % (k, ref, alpha, r2, m.mean(), m.std()))

if len(rows) >= 2:
    al = np.array([r[2] for r in rows]); r2 = np.array([r[3] for r in rows])
    print("\n  alpha across all BLUR frames: mean=%.4f std=%.4f min=%.4f max=%.4f" % (al.mean(), al.std(), al.min(), al.max()))
    print("  R2 across all BLUR frames : mean=%.4f std=%.4f min=%.4f max=%.4f" % (r2.mean(), r2.std(), r2.min(), r2.max()))

print("\n=== consecutive-BLUR alpha map correlation (coherent state vs random dither) ===")
for b in batches:
    bs = [k for k in b if state[k] == "BLUR"]
    for i in range(len(bs)-1):
        k1, k2 = bs[i], bs[i+1]
        maps = {}
        for k in (k1, k2):
            ss = [x for x in b if state[x] == "SHARP"]
            if not ss: break
            ref = min(ss, key=lambda s: abs(s-k))
            A = lum(F[k]).astype(np.float64); B = lum(F[ref]).astype(np.float64)
            with np.errstate(divide="ignore", invalid="ignore"):
                al = (A-B)/np.maximum(255.0-B, 1.0)
            maps[k] = np.nan_to_num(al, nan=0.0, posinf=0.0, neginf=0.0)[ROI[2]:ROI[3], ROI[0]:ROI[1]].ravel()
        if len(maps) == 2:
            r = float(np.corrcoef(maps[k1], maps[k2])[0,1])
            print("  %d vs %d : alpha-map r=%+.3f" % (k1, k2, r))
