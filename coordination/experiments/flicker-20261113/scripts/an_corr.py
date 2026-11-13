# -*- coding: utf-8 -*-
"""Per-frame correlation between the veil state and every engine-side counter.

State comes from the bimodal brightness of the pure-cobblestone ROI: the veil
raises the mean and lowers the Laplacian variance, so within each captured
batch the frames split cleanly into a bright/low-lap group (BLUR) and a
dim/high-lap group (SHARP). We then ask, per frame, which engine counter
predicts that state.
"""
import numpy as np, os, glob, sys, re, collections, math

FRAME_DIR = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ffrs-flicker-csv/more"
LOG      = sys.argv[2] if len(sys.argv) > 2 else "/tmp/ffrs-flicker-csv/engine7.log"
ROI      = (260, 560, 400, 540)

def load(p):
    raw = open(p, "rb").read()
    i = raw.index(b"\n"); hdr = raw[:i].decode("latin1"); body = raw[i+1:]
    d = dict(kv.split("=", 1) for kv in hdr.split() if "=" in kv)
    w = int(d["width"]); h = int(d["height"]); bpp = int(d["bpp"])
    return hdr, np.frombuffer(body[:w*h*bpp], dtype=np.uint8).reshape(h, w, bpp)

def lum(a):
    return (a[:, :, :3].astype(np.float32) * np.array([0.299,0.587,0.114], dtype=np.float32)).sum(2)

def lapv(r):
    k = r[1:-1,1:-1]*-4 + r[:-2,1:-1] + r[2:,1:-1] + r[1:-1,:-2] + r[1:-1,2:]
    return float(k.var())

frames = {}
for p in sorted(glob.glob(os.path.join(FRAME_DIR, "*.bin"))):
    hdr, a = load(p)
    present = int(hdr.split("present=")[1].split()[0])
    l = lum(a)
    r = l[ROI[2]:ROI[3], ROI[0]:ROI[1]]
    frames[present] = {"mean": float(r.mean()), "lap": lapv(r)}

ks = sorted(frames)
batches, cur = [], [ks[0]]
for k in ks[1:]:
    if k == cur[-1] + 1: cur.append(k)
    else: batches.append(cur); cur = [k]
batches.append(cur)

# --- state by within-batch bimodal split on mean (veil raises mean) ---
state = {}
for batch in batches:
    means = sorted((frames[k]["mean"], k) for k in batch)
    vals = np.array([m for m, _ in means])
    # split at the largest gap in the sorted means
    gaps = np.diff(vals)
    if len(gaps) == 0:
        for _, k in means: state[k] = "SHARP"
        continue
    cut = int(np.argmax(gaps))
    if gaps[cut] < 3.0:
        for _, k in means: state[k] = "SHARP"
        continue
    hi = {k for _, k in means[cut+1:]}
    for _, k in means:
        state[k] = "BLUR" if k in hi else "SHARP"

print("=== state per frame (bimodal mean split) ===")
for b in batches:
    print("  batch %d:" % b[0], " ".join("%d:%s(%.0f/%.0f)" % (k, state[k], frames[k]["mean"], frames[k]["lap"]) for k in b))

sharp = [k for k in ks if state[k] == "SHARP"]
blur  = [k for k in ks if state[k] == "BLUR"]
print("\nSHARP(%d) BLUR(%d)" % (len(sharp), len(blur)))

# --- engine log ---
log = open(LOG, "r", errors="replace").read()
dr_re = re.compile(r"FLAME_DR fr=(\d+) part=(\d+) pmem=(\S+) pf=(\d+) over=(\d+) (.*)")
ff_re = re.compile(r"FLAME_FRAME fr=(\d+) (.*)")
dr, dr_pf = collections.defaultdict(dict), {}
for m in dr_re.finditer(log):
    fr = int(m.group(1)); dr_pf[fr] = int(m.group(4))
    for tok in m.group(6).split(","):
        p = tok.split(":")
        if len(p) < 5: continue
        dr[fr][(p[0], p[1], p[2])] = (int(p[3]), int(p[4]))
eng = {int(m.group(1)): m.group(2) for m in ff_re.finditer(log)}

def corr(x, y):
    x = np.asarray(x, dtype=np.float64); y = np.asarray(y, dtype=np.float64)
    if len(x) < 4: return 0.0, 0.0
    if x.std() < 1e-12: return 0.0, 0.0
    r = float(np.corrcoef(x, y)[0, 1])
    return r, r*r

lab = np.array([1.0 if state[k] == "BLUR" else 0.0 for k in ks])

print("\n=== census entries vs state (point-biserial r, |r| desc) ===")
rows = []
allkeys = set()
for k in ks: allkeys |= set(dr.get(k, {}))
for key in allkeys:
    n = [dr.get(k, {}).get(key, (0, 0))[0] for k in ks]
    i = [dr.get(k, {}).get(key, (0, 0))[1] for k in ks]
    rn, _ = corr(n, lab); ri, _ = corr(i, lab)
    if abs(rn) > 0.15 or abs(ri) > 0.15:
        rows.append((max(abs(rn), abs(ri)), key, rn, ri, n, i))
rows.sort(reverse=True)
for mag, key, rn, ri, n, i in rows:
    sn = np.mean([dr.get(k,{}).get(key,(0,0))[0] for k in sharp])
    bn = np.mean([dr.get(k,{}).get(key,(0,0))[0] for k in blur])
    si = np.mean([dr.get(k,{}).get(key,(0,0))[1] for k in sharp])
    bi = np.mean([dr.get(k,{}).get(key,(0,0))[1] for k in blur])
    print("  %-42s r_n=%+.3f r_i=%+.3f  n: SHARP=%6.2f BLUR=%6.2f | inst: SHARP=%9.2f BLUR=%9.2f"
          % (str(key), rn, ri, sn, bn, si, bi))
if not rows: print("  (no census entry correlates with |r|>0.15)")

print("\n=== FLAME_FRAME scalars vs state ===")
allf = collections.defaultdict(list)
for k in ks:
    if k not in eng: continue
    d = dict(kv.split("=",1) for kv in eng[k].split() if "=" in kv)
    for f, v in d.items(): allf[f].append(v)
srows = []
for f, vals in allf.items():
    if len(vals) != len(ks): continue
    try: x = [float(v) for v in vals]
    except Exception: continue
    r, _ = corr(x, lab)
    srows.append((abs(r), f, r, x))
srows.sort(reverse=True)
for mag, f, r, x in srows[:20]:
    print("  %-12s r=%+.3f  min=%.6g max=%.6g" % (f, r, min(x), max(x)))

# --- total draws per frame vs state ---
tot = [sum(v[0] for v in dr.get(k, {}).values()) for k in ks]
r, _ = corr(tot, lab)
print("\ntotal draws/frame vs state: r=%+.3f  SHARP=%.1f BLUR=%.1f"
      % (r, np.mean([sum(v[0] for v in dr.get(k,{}).values()) for k in sharp]),
                np.mean([sum(v[0] for v in dr.get(k,{}).values()) for k in blur])))
toti = [sum(v[1] for v in dr.get(k, {}).values()) for k in ks]
r, _ = corr(toti, lab)
print("total instances/frame vs state: r=%+.3f  SHARP=%.1f BLUR=%.1f"
      % (r, np.mean([sum(v[1] for v in dr.get(k,{}).values()) for k in sharp]),
                np.mean([sum(v[1] for v in dr.get(k,{}).values()) for k in blur])))
pf = [dr_pf.get(k, 0) for k in ks]
r, _ = corr(pf, lab)
print("pf (present ROI fire px) vs state: r=%+.3f  SHARP=%.1f BLUR=%.1f"
      % (r, np.mean([dr_pf.get(k,0) for k in sharp]), np.mean([dr_pf.get(k,0) for k in blur])))

# --- dump the per-frame table for the strongest census candidates ---
print("\n=== per-frame table (strongest candidates) ===")
cands = [r[1] for r in rows[:6]]
print("present  state  mean   lap  " + "  ".join("|".join(c) for c in cands))
for idx, k in enumerate(ks):
    print("%7d %-5s %6.1f %6.0f  %s" % (k, state[k], frames[k]["mean"], frames[k]["lap"],
        "  ".join("%3d" % dr.get(k,{}).get(c,(0,0))[0] for c in cands)))
