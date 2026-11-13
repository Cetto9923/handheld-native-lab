# -*- coding: utf-8 -*-
"""Robust SHARP/BLUR classification + per-frame census alignment.

The pixel state is strictly bimodal (bright+flat = BLUR with the white veil,
dim+sharp = SHARP), so classify by a 1-D threshold on the lapvar of the pure
cobblestone ROI, then order each frame's census entries and diff the two
states element by element.
"""
import numpy as np, os, glob, sys, re, collections

FRAME_DIR = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ffrs-flicker-csv/more"
LOG      = sys.argv[2] if len(sys.argv) > 2 else "/tmp/ffrs-flicker-csv/engine6.log"
ROI      = (260, 560, 400, 540)   # x0,x1,y0,y1 pure cobblestone, protagonist excluded

def load(p):
    raw = open(p, "rb").read()
    i = raw.index(b"\n"); hdr = raw[:i].decode("latin1"); body = raw[i+1:]
    d = dict(kv.split("=", 1) for kv in hdr.split() if "=" in kv)
    w = int(d["width"]); h = int(d["height"]); bpp = int(d["bpp"])
    return hdr, np.frombuffer(body[:w*h*bpp], dtype=np.uint8).reshape(h, w, bpp)

def lum(a):
    return (a[:, :, :3].astype(np.float32) * np.array([0.299,0.587,0.114], dtype=np.float32)).sum(2)

def lapvar(r):
    k = r[1:-1,1:-1]*-4 + r[:-2,1:-1] + r[2:,1:-1] + r[1:-1,:-2] + r[1:-1,2:]
    return float(k.var())

frames = {}
for p in sorted(glob.glob(os.path.join(FRAME_DIR, "*.bin"))):
    hdr, a = load(p)
    present = int(hdr.split("present=")[1].split()[0])
    l = lum(a)
    x0, x1, y0, y1 = ROI
    frames[present] = {"mean": float(l[y0:y1, x0:x1].mean()), "lap": lapvar(l[y0:y1, x0:x1]), "lum": l}

ks = sorted(frames)
laps = np.array([frames[k]["lap"] for k in ks])
# Otsu-style split on the lapvar histogram: the states are far apart (150 vs 240)
thr = (laps.min() + laps.max()) / 2.0
# refine: choose the threshold that maximises between-class variance
best = None
for t in np.unique(laps):
    a1 = laps[laps <= t]; a2 = laps[laps > t]
    if len(a1) < 1 or len(a2) < 1: continue
    w1, w2 = len(a1)/len(laps), len(a2)/len(laps)
    v = w1*w2*(a1.mean()-a2.mean())**2
    if best is None or v > best[1]: best = (t, v)
thr = best[0]
state = {k: ("SHARP" if frames[k]["lap"] <= thr else "BLUR") for k in ks}
print("=== pixel states (lap threshold=%.1f) ===" % thr)
for k in ks:
    print("  %6d  %-5s mean=%7.2f lap=%8.2f" % (k, state[k], frames[k]["mean"], frames[k]["lap"]))
sharp = [k for k in ks if state[k] == "SHARP"]
blur  = [k for k in ks if state[k] == "BLUR"]
print("\nSHARP(%d): %s" % (len(sharp), sharp))
print("BLUR (%d): %s" % (len(blur), blur))

# ---- engine log ----
log = open(LOG, "r", errors="replace").read()
dr_re  = re.compile(r"FLAME_DR fr=(\d+) part=(\d+) pmem=(\S+) pf=(\d+) over=(\d+) (.*)")
ev_re  = re.compile(r"FLAME_EV fr=(\d+) part=(\d+) over=(\d+) (.*)")
ff_re  = re.compile(r"FLAME_FRAME fr=(\d+) (.*)")

dr, dr_over, dr_pf = collections.defaultdict(dict), {}, {}
for m in dr_re.finditer(log):
    fr = int(m.group(1))
    dr_over[fr] = max(dr_over.get(fr, 0), int(m.group(5)))
    dr_pf[fr]   = int(m.group(4))
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
eng_frame = {int(m.group(1)): m.group(2) for m in ff_re.finditer(log)}

cov = [(k, len(dr.get(k, {})), dr_over.get(k, -1), dr_pf.get(k, -1)) for k in ks]
print("\n=== census coverage (entries, overflow, pf) ===")
for k, n, ov, pf in cov:
    print("  %6d %-5s entries=%3d over=%3d pf=%5d" % (k, state[k], n, ov, pf))

bad = [c for c in cov if c[2] > 0]
print("frames with census overflow:", len(bad))
sizes = collections.Counter(c[1] for c in cov)
print("entry-count distribution:", dict(sizes))

# per-frame pf vs state  -- is the fire-pixel count the discriminator?
print("\n=== pf (fire pixels) by state ===")
for st in ("SHARP", "BLUR"):
    vals = [dr_pf[k] for k in ks if state[k] == st and k in dr_pf]
    if vals: print("  %-5s n=%d min=%d max=%d mean=%.1f" % (st, len(vals), min(vals), max(vals), sum(vals)/len(vals)))

# ---- set difference of draw entries, ignoring order ----
def eset(frs):
    s = set()
    for f in frs:
        for key in dr.get(f, {}): s.add(key)
    return s
S, B = eset(sharp), eset(blur)
print("\n=== draw-entry SET difference ===")
print("entries only in SHARP:", len(S - B))
for key in sorted(S - B): print("   ", key)
print("entries only in BLUR :", len(B - S))
for key in sorted(B - S): print("   ", key)
print("common:", len(S & B))

# ---- per-frame totals ----
def totals(frs):
    tot_n = 0; tot_inst = 0; n = 0
    for f in frs:
        for (nn, ii) in dr.get(f, {}).values():
            tot_n += nn; tot_inst += ii
        n += 1
    return tot_n/max(n,1), tot_inst/max(n,1)
sn, si = totals(sharp); bn, bi = totals(blur)
print("\n=== per-frame averages ===")
print("  SHARP draws/frame=%.1f inst/frame=%.1f" % (sn, si))
print("  BLUR  draws/frame=%.1f inst/frame=%.1f" % (bn, bi))

# ---- entries whose per-frame count differs between the two states ----
def perframe(frs):
    a = {}
    for f in frs:
        for key, (n, i) in dr.get(f, {}).items():
            an, ai = a.get(key, (0, 0))
            a[key] = (an + n / len(frs), ai + i / len(frs))
    return a
as_, ab = perframe(sharp), perframe(blur)
print("\n=== entries whose per-frame draw count differs between states ===")
for key in sorted(set(as_) | set(ab)):
    x = as_.get(key, (0, 0)); y = ab.get(key, (0, 0))
    if abs(x[0]-y[0]) > 0.01 or abs(x[1]-y[1]) > 0.01:
        print("   %-42s SHARP n=%6.2f inst=%8.2f | BLUR n=%6.2f inst=%8.2f" % (str(key), x[0], x[1], y[0], y[1]))

# ---- FLAME_FRAME scalar fields ----
if sharp and blur:
    print("\n=== FLAME_FRAME field medians by state ===")
    fields = collections.defaultdict(lambda: ([], []))
    for k in ks:
        if k not in eng_frame: continue
        d = dict(kv.split("=", 1) for kv in eng_frame[k].split() if "=" in kv)
        for f, v in d.items():
            fields[f][0 if state[k] == "SHARP" else 1].append(v)
    for f in sorted(fields):
        s, b = fields[f]
        if not s or not b: continue
        def med(v):
            try: return np.median([float(x) for x in v])
            except Exception: return None
        ms, mb = med(s), med(b)
        if ms is None: continue
        if abs(ms - mb) > 1e-9:
            print("   %-10s SHARP=%-16.4g BLUR=%-16.4g  delta=%.4g" % (f, ms, mb, ms - mb))
