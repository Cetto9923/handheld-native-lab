# -*- coding: utf-8 -*-
"""Ordered draw list, reassembled in part order (the log chunks at ~1100 bytes)."""
import numpy as np, os, glob, sys, re, collections

FRAME_DIR = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ffrs-flicker-csv/more"
LOG      = sys.argv[2] if len(sys.argv) > 2 else "/tmp/ffrs-flicker-csv/engine7.log"

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
ROI = (260, 560, 400, 540)
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
S = [k for k in ks if state[k] == "SHARP"]; B = [k for k in ks if state[k] == "BLUR"]

log = open(LOG, "r", errors="replace").read()
parts = collections.defaultdict(dict)     # frame -> part -> token list
pf_of = {}
for m in re.finditer(r"FLAME_DR fr=(\d+) part=(\d+) pmem=(\S+) pf=(\d+) over=(\d+) (.*)", log):
    fr = int(m.group(1)); pt = int(m.group(2)); pf_of[fr] = int(m.group(4))
    parts[fr][pt] = [t for t in m.group(6).split(",") if ":" in t]
ordered = {}
for fr, pm in parts.items():
    seq = []
    for pt in sorted(pm): seq.extend(pm[pt])
    ordered[fr] = seq

print("=== reassembled ordered draw lists ===")
lens = collections.Counter(len(ordered.get(k, [])) for k in ks)
print("  length distribution over %d frames: %s" % (len(ks), dict(lens)))

# compare within-state (baseline) vs across-state
def cmp(a, b):
    if len(a) != len(b): return "length %d vs %d" % (len(a), len(b))
    pos = [i for i in range(len(a)) if a[i] != b[i]]
    if not pos: return "identical"
    return "%d/%d positions differ, first: %s" % (len(pos), len(a),
          "; ".join("%d %s vs %s" % (i, a[i], b[i]) for i in pos[:3]))

print("\n  --- SHARP vs SHARP (baseline: how stable is the list between frames?) ---")
n_id = 0; n_all = 0; samples = []
for i in range(len(S)):
    for j in range(i+1, len(S)):
        r = cmp(ordered.get(S[i], []), ordered.get(S[j], []))
        n_all += 1
        if r == "identical": n_id += 1
        elif len(samples) < 3: samples.append((S[i], S[j], r))
print("     identical: %d/%d pairs" % (n_id, n_all))
for a, b, r in samples: print("       %d vs %d : %s" % (a, b, r))

print("\n  --- BLUR vs BLUR ---")
n_id = 0; n_all = 0; samples = []
for i in range(len(B)):
    for j in range(i+1, len(B)):
        r = cmp(ordered.get(B[i], []), ordered.get(B[j], []))
        n_all += 1
        if r == "identical": n_id += 1
        elif len(samples) < 3: samples.append((B[i], B[j], r))
print("     identical: %d/%d pairs" % (n_id, n_all))
for a, b, r in samples: print("       %d vs %d : %s" % (a, b, r))

print("\n  --- SHARP vs BLUR ---")
n_id = 0; n_all = 0; samples = []
for a in S:
    for b in B:
        r = cmp(ordered.get(a, []), ordered.get(b, []))
        n_all += 1
        if r == "identical": n_id += 1
        elif len(samples) < 5: samples.append((a, b, r))
print("     identical: %d/%d pairs" % (n_id, n_all))
for a, b, r in samples: print("       %d vs %d : %s" % (a, b, r))

print("\n=== engine metric separation (independent of our ROI choice) ===")
eng = {int(m.group(1)): dict(kv.split("=",1) for kv in m.group(2).split() if "=" in kv)
       for m in re.finditer(r"FLAME_FRAME fr=(\d+) (.*)", log)}
for name in ("pmean", "rtpx", "pf"):
    sv = [int(eng[k][name]) for k in S if k in eng and name in eng[k]]
    bv = [int(eng[k][name]) for k in B if k in eng and name in eng[k]]
    if not sv or not bv: continue
    ov = set(sv) & set(bv)
    print("  %-6s SHARP=[%d..%d] n=%d | BLUR=[%d..%d] n=%d | overlap=%d"
          % (name, min(sv), max(sv), len(sv), min(bv), max(bv), len(bv), len(ov)))
    if name == "pmean":
        print("      SHARP: %s" % sorted(sv))
        print("      BLUR : %s" % sorted(bv))
    if name == "rtpx":
        print("      SHARP: %s" % sorted(sv))
        print("      BLUR : %s" % sorted(bv))
