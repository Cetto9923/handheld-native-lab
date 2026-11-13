# -*- coding: utf-8 -*-
"""Classify each presented frame as SHARP or BLUR by fitting the veil model.

The veil is a translucent white overlay on the ground geometry:
    BLUR = a * SHARP + b   with a ~= 0.9265, b ~= 17.1  (alpha ~= 0.07 of white)

A scene drift (lighting change, animation) instead produces a ~= 1 with a
different b, so the slope is the discriminator, not the offset. We fit every
frame against the per-batch SHARP reference and look at the slope.

Frame-to-frame the veil toggles, so within one captured batch we can find a
consistent reference. We use the frame with the highest lapvar (sharpest) in
each batch as the SHARP reference and fit every other frame against it.
"""
import numpy as np, os, glob, sys, re, collections

FRAME_DIR = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ffrs-flicker-csv/more"
LOG      = sys.argv[2] if len(sys.argv) > 2 else "/tmp/ffrs-flicker-csv/engine7.log"
ROI      = (260, 560, 400, 540)   # x0,x1,y0,y1 pure cobblestone

def load(p):
    raw = open(p, "rb").read()
    i = raw.index(b"\n"); hdr = raw[:i].decode("latin1"); body = raw[i+1:]
    d = dict(kv.split("=", 1) for kv in hdr.split() if "=" in kv)
    w = int(d["width"]); h = int(d["height"]); bpp = int(d["bpp"])
    return hdr, np.frombuffer(body[:w*h*bpp], dtype=np.uint8).reshape(h, w, bpp)

def lum(a):
    return (a[:, :, :3].astype(np.float32) * np.array([0.299,0.587,0.114], dtype=np.float32)).sum(2)

frames = {}
for p in sorted(glob.glob(os.path.join(FRAME_DIR, "*.bin"))):
    hdr, a = load(p)
    present = int(hdr.split("present=")[1].split()[0])
    l = lum(a)
    x0, x1, y0, y1 = ROI
    r = l[y0:y1, x0:x1]
    frames[present] = {"mean": float(r.mean()), "lum": l,
                        "flat": r.ravel().astype(np.float64)}

ks = sorted(frames)

def fit(ref, tgt):
    """Fit tgt = a*ref + b, return (a, b, r2, rms)."""
    x = ref; y = tgt
    n = len(x)
    sx = x.sum(); sy = y.sum()
    sxx = (x*x).sum(); sxy = (x*y).sum()
    den = n*sxx - sx*sx
    if abs(den) < 1e-9: return (1.0, 0.0, 0.0, 0.0)
    a = (n*sxy - sx*sy) / den
    b = (sy - a*sx) / n
    pred = a*x + b
    resid = y - pred
    ss_res = (resid*resid).sum()
    ss_tot = ((y - y.mean())**2).sum()
    r2 = 1.0 - ss_res/ss_tot if ss_tot > 0 else 0.0
    return (a, b, r2, float(np.sqrt((resid*resid).mean())))

# group into batches of consecutive presents
batches = []
cur = [ks[0]]
for k in ks[1:]:
    if k == cur[-1] + 1: cur.append(k)
    else: batches.append(cur); cur = [k]
batches.append(cur)
print("batches:", [(b[0], b[-1], len(b)) for b in batches])

labels = {}
print("\n=== per-frame veil fit (reference = sharpest frame of the batch) ===")
print("present   mean    a        b        R2      rms     verdict")
for batch in batches:
    ref = max(batch, key=lambda k: frames[k]["mean"] * 0 + 0)  # placeholder
    # sharpest = highest high-frequency energy: use variance of Laplacian
    def lapv(k):
        r = frames[k]["lum"][ROI[2]:ROI[3], ROI[0]:ROI[1]]
        kk = r[1:-1,1:-1]*-4 + r[:-2,1:-1] + r[2:,1:-1] + r[1:-1,:-2] + r[1:-1,2:]
        return float(kk.var())
    ref = max(batch, key=lapv)
    for k in batch:
        if k == ref:
            labels[k] = "REF"
            print("%7d %7.2f  (reference)" % (k, frames[k]["mean"]))
            continue
        a, b, r2, rms = fit(frames[ref]["flat"], frames[k]["flat"])
        veiled = (a < 0.97) and (r2 > 0.80)
        labels[k] = "BLUR" if veiled else "SHARP"
        print("%7d %7.2f  %7.4f %8.2f %7.4f %7.2f  %s" % (k, frames[k]["mean"], a, b, r2, rms, labels[k]))

sharp = [k for k in ks if labels.get(k) == "SHARP"]
blur  = [k for k in ks if labels.get(k) == "BLUR"]
print("\nSHARP(%d): %s" % (len(sharp), sharp))
print("BLUR (%d): %s" % (len(blur), blur))

# ---- census ----
log = open(LOG, "r", errors="replace").read()
dr_re = re.compile(r"FLAME_DR fr=(\d+) part=(\d+) pmem=(\S+) pf=(\d+) over=(\d+) (.*)")
ev_re = re.compile(r"FLAME_EV fr=(\d+) part=(\d+) over=(\d+) (.*)")
ff_re = re.compile(r"FLAME_FRAME fr=(\d+) (.*)")
dr, dr_over, dr_pf = collections.defaultdict(dict), {}, {}
for m in dr_re.finditer(log):
    fr = int(m.group(1))
    dr_over[fr] = max(dr_over.get(fr, 0), int(m.group(5)))
    dr_pf[fr] = int(m.group(4))
    for tok in m.group(6).split(","):
        p = tok.split(":")
        if len(p) < 5: continue
        dr[fr][(p[0], p[1], p[2])] = (int(p[3]), int(p[4]))
ev = collections.defaultdict(dict)
for m in ev_re.finditer(log):
    fr = int(m.group(1))
    for tok in m.group(4).split(","):
        p = tok.split(":")
        if len(p) < 4: continue
        ev[fr][(p[0], p[1], p[2])] = int(p[3])
eng = {int(m.group(1)): m.group(2) for m in ff_re.finditer(log)}

print("\n=== census coverage ===")
miss = [k for k in ks if k not in dr or not dr[k]]
print("frames without census:", len(miss), miss[:10])
for k in ks:
    if k in dr:
        print("  %6d %-5s entries=%3d over=%3d pf=%5d" % (k, labels.get(k), len(dr[k]), dr_over.get(k,-1), dr_pf.get(k,-1)))

def eset(frs):
    s = set()
    for f in frs:
        for key in dr.get(f, {}): s.add(key)
    return s
S, B = eset(sharp), eset(blur)
print("\n=== draw-entry SET difference (complete census, over=0) ===")
print("only in SHARP (%d):" % len(S-B))
for key in sorted(S-B): print("   ", key, [ (f, dr[f][key]) for f in sharp if key in dr.get(f,{}) ][:4])
print("only in BLUR (%d):" % len(B-S))
for key in sorted(B-S): print("   ", key, [ (f, dr[f][key]) for f in blur if key in dr.get(f,{}) ][:4])
print("common: %d" % len(S&B))

def perframe(frs):
    a = {}
    n = max(len(frs),1)
    for f in frs:
        for key, (nn, ii) in dr.get(f, {}).items():
            an, ai = a.get(key, (0.0, 0.0))
            a[key] = (an + nn/n, ai + ii/n)
    return a
as_, ab = perframe(sharp), perframe(blur)
print("\n=== entries whose per-frame draw count differs between states ===")
for key in sorted(set(as_)|set(ab)):
    x = as_.get(key,(0,0)); y = ab.get(key,(0,0))
    if abs(x[0]-y[0])>0.02 or abs(x[1]-y[1])>0.02:
        print("   %-42s SHARP n=%7.2f inst=%9.2f | BLUR n=%7.2f inst=%9.2f" % (str(key),x[0],x[1],y[0],y[1]))

print("\n=== event differences ===")
agg = {}
for grp, frs in (("SHARP",sharp),("BLUR",blur)):
    a = {}
    for f in frs:
        for key, n in ev.get(f, {}).items(): a[key] = a.get(key,0)+n
    agg[grp] = a
allk = sorted(set(agg["SHARP"])|set(agg["BLUR"]))
for key in allk:
    s = agg["SHARP"].get(key,0); b = agg["BLUR"].get(key,0)
    if s != b: print("   %-40s SHARP=%-8d BLUR=%-8d" % (str(key), s, b))
if not any(agg["SHARP"].get(k,0)!=agg["BLUR"].get(k,0) for k in allk): print("   (none)")

print("\n=== FLAME_FRAME scalar medians by state ===")
fields = collections.defaultdict(lambda: ([], []))
for k in ks:
    if k not in eng: continue
    d = dict(kv.split("=",1) for kv in eng[k].split() if "=" in kv)
    for f, v in d.items():
        fields[f][0 if labels.get(k)=="SHARP" else 1].append(v)
for f in sorted(fields):
    s, b = fields[f]
    if not s or not b: continue
    try:
        ms = np.median([float(x) for x in s]); mb = np.median([float(x) for x in b])
    except Exception: continue
    if abs(ms-mb) > 1e-9:
        print("   %-10s SHARP=%-16.6g BLUR=%-16.6g delta=%.6g" % (f, ms, mb, ms-mb))
