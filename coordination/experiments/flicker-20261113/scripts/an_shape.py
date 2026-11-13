# -*- coding: utf-8 -*-
"""Reconstruct the veil's spatial shape and test whether it is static."""
import numpy as np, os, glob, sys, re, collections
sys.path.insert(0, "/tmp/ffrs-flicker-csv")

FRAME_DIR = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ffrs-flicker-csv/more"
OUT = sys.argv[2] if len(sys.argv) > 2 else "/tmp/ffrs-flicker-csv/shape"

def load(p):
    raw = open(p, "rb").read()
    i = raw.index(b"\n"); hdr = raw[:i].decode("latin1"); body = raw[i+1:]
    d = dict(kv.split("=", 1) for kv in hdr.split() if "=" in kv)
    w = int(d["width"]); h = int(d["height"]); bpp = int(d["bpp"])
    return hdr, np.frombuffer(body[:w+1, h+1, bpp][:h, :w], dtype=np.uint8) if False else \
           np.frombuffer(body[:w*h*bpp], dtype=np.uint8).reshape(h, w, bpp)

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

def mean_of(a, box):
    x0,x1,y0,y1 = box
    return float(lum(a)[y0:y1, x0:x1].mean())

ROI = (260, 560, 400, 540)
state = {}
for b in batches:
    vals = sorted((mean_of(F[k], ROI), k) for k in b)
    arr = np.array([v for v, _ in vals])
    g = np.diff(arr)
    cut = int(np.argmax(g)) if len(g) else -1
    if cut < 0 or g[cut] < 3.0:
        for _, k in vals: state[k] = "SHARP"
    else:
        hi = {k for _, k in vals[cut+1:]}
        for _, k in vals: state[k] = "BLUR" if k in hi else "SHARP"

print("states:", {k: state[k] for k in ks})

# Build alpha maps: for each BLUR frame, pair with the nearest SHARP frame in the batch
os.makedirs(OUT, exist_ok=True)
alpha_maps = {}
for b in batches:
    bs = [k for k in b if state[k] == "BLUR"]
    ss = [k for k in b if state[k] == "SHARP"]
    if not bs or not ss: continue
    for k in bs:
        ref = min(ss, key=lambda s: abs(s - k))
        A = lum(F[k]).astype(np.float64); B = lum(F[ref]).astype(np.float64)
        # veil model: blurred = (1-alpha)*ref + alpha*white  ->  alpha = (A-B)/(255-B)
        with np.errstate(divide="ignore", invalid="ignore"):
            al = (A - B) / np.maximum(255.0 - B, 1.0)
        al = np.nan_to_num(al, nan=0.0, posinf=0.0, neginf=0.0)
        alpha_maps[k] = (al, ref)

print("\n=== alpha map stats ===")
for k, (al, ref) in alpha_maps.items():
    m = al[400:540, 260:560]
    print("  %6d (ref %6d)  alpha ROI mean=%+.4f  p05=%+.4f p50=%+.4f p95=%+.4f  max=%+.4f"
          % (k, ref, m.mean(), np.percentile(m,5), np.percentile(m,50), np.percentile(m,95), m.max()))

# Are the alpha maps the SAME shape across BLUR frames? (static decal) or moving?
print("\n=== pairwise alpha-map correlation (are the shapes identical?) ===")
keys = sorted(alpha_maps)
if len(keys) >= 2:
    for i in range(len(keys)):
        for j in range(i+1, len(keys)):
            a1 = alpha_maps[keys[i]][0][400:540, 260:560].ravel()
            a2 = alpha_maps[keys[j]][0][400:540, 260:560].ravel()
            c = float(np.corrcoef(a1, a2)[0,1])
            print("  %d vs %d : r=%+.3f" % (keys[i], keys[j], c))

# Where exactly? Full-image alpha extent for one strong pair.
print("\n=== full-image veil extent (strongest pair) ===")
best = None
for k, (al, ref) in alpha_maps.items():
    m = al[400:540, 260:560].mean()
    if best is None or m > best[0]: best = (m, k, al, ref)
m, k, al, ref = best
print("pair blur=%d ref=%d" % (k, ref))
strong = al > 0.03
ys, xs = np.nonzero(strong)
if len(xs):
    print("  alpha>0.03 count=%d  x[%d..%d] y[%d..%d]" % (len(xs), xs.min(), xs.max(), ys.min(), ys.max()))
    # column profile
    colsum = strong.sum(0)
    rowsum = strong.sum(1)
    def prof(v, step, name):
        out = []
        for i in range(0, len(v), step):
            seg = v[i:i+step]
            out.append("%d:%d" % (i, int(seg.sum())))
        print("  %s profile (sum per %d px): %s" % (name, step, " ".join(out)))
    prof(colsum, 64, "x")
    prof(rowsum, 64, "y")

# alpha channel of the present image: did the composite alpha change?
print("\n=== present image alpha channel ===")
for k in ks[:6]:
    a = F[k]
    al = a[:,:,3]
    print("  %6d %-5s alpha min=%d max=%d mean=%.2f  nonzero=%.3f" % (k, state[k], al.min(), al.max(), al.mean(), (al>0).mean()))
