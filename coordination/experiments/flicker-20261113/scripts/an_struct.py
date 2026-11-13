# -*- coding: utf-8 -*-
"""Structure of the veil: is the alpha constant over the ground, or does it
follow the cobblestone detail (mortar lines)?

If alpha is roughly constant over the whole ground region, some constant was
added (a blend factor, a fog term, a uniform). If alpha tracks the mortar
lines (high-frequency structure), the ground texture itself is being sampled
differently (mip / LOD / filter).
"""
import numpy as np, os, glob, sys

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

# use the best-fitting pair (highest R2) for the clearest structural read
best = None
for b in batches:
    bs = [k for k in b if state[k] == "BLUR"]; ss = [k for k in b if state[k] == "SHARP"]
    if not bs or not ss: continue
    for k in bs:
        ref = min(ss, key=lambda s: abs(s-k))
        A = lum(F[k]).astype(np.float64); B = lum(F[ref]).astype(np.float64)
        with np.errstate(divide="ignore", invalid="ignore"):
            al = (A-B)/np.maximum(255.0-B, 1.0)
        al = np.nan_to_num(al, nan=0.0, posinf=0.0, neginf=0.0)
        x = B[ROI[2]:ROI[3], ROI[0]:ROI[1]].ravel(); y = A[ROI[2]:ROI[3], ROI[0]:ROI[1]].ravel()
        w = np.maximum(255.0-x, 1.0)
        a = float(np.sum(w*(y-x))/np.sum(w*w)); pred = x + a*w
        r2 = 1-float(np.sum((y-pred)**2))/float(np.sum((y-y.mean())**2))
        if best is None or r2 > best[0]: best = (r2, k, ref, al, a)

r2, kb, rref, AL, alpha = best
print("best pair blur=%d ref=%d alpha=%.4f R2=%.4f" % (kb, rref, alpha, r2))

# Is alpha correlated with the SHARP frame's local brightness? (a pure
# multiplicative+additive veil predicts alpha is flat; a detail-driven change
# makes alpha correlate with the texture's high-frequency content.)
x = lum(F[rref])[ROI[2]:ROI[3], ROI[0]:ROI[1]]
a = AL[ROI[2]:ROI[3], ROI[0]:ROI[1]]
print("\nalpha vs SHARP brightness: corr=%+.3f" % float(np.corrcoef(a.ravel(), x.ravel())[0,1]))
k = np.array([[0,1,0],[1,-4,1],[0,1,0]], dtype=np.float32)
hp = np.zeros_like(x[1:-1,1:-1]); hp[:,:] = (x[:-2,1:-1]+x[2:,1:-1]+x[1:-1,:-2]+x[1:-1,2:]-4*x[1:-1,1:-1])
print("alpha vs SHARP high-frequency (mortar lines): corr=%+.3f" % float(np.corrcoef(a[1:-1,1:-1].ravel(), hp.ravel())[0,1]))
print("alpha: mean=%.4f std=%.4f p05=%.4f p50=%.4f p95=%.4f" % (a.mean(), a.std(), np.percentile(a,5), np.percentile(a,50), np.percentile(a,95)))
print("SHARP brightness in ROI: mean=%.1f std=%.1f" % (x.mean(), x.std()))

# Per-region alpha: does it vary with distance/position on the ground?
print("\nalpha by 40x40 tile over the ROI (mean alpha):")
for yy in range(0, a.shape[0], 40):
    row = []
    for xx in range(0, a.shape[1], 40):
        row.append("%5.3f" % a[yy:yy+40, xx:xx+40].mean())
    print("  " + " ".join(row))
print("\nalpha by tile std:")
for yy in range(0, a.shape[0], 40):
    row = []
    for xx in range(0, a.shape[1], 40):
        row.append("%5.3f" % a[yy:yy+40, xx:xx+40].std())
    print("  " + " ".join(row))
