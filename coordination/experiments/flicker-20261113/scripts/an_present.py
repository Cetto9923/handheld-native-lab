# -*- coding: utf-8 -*-
"""Which draws write the presented image, and do they differ between states?

The veil only shows up on the ground, so the draw that paints the ground is
either the one writing the present target ('P') or one writing the scene
target that the present image copies. We list the draws that write the
present target and compare their per-frame instance counts across the two
states, which is the last piece of draw-level evidence available from the
census alone.
"""
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
parts = collections.defaultdict(dict)
for m in re.finditer(r"FLAME_DR fr=(\d+) part=(\d+) pmem=(\S+) pf=(\d+) over=(\d+) (.*)", log):
    fr = int(m.group(1)); pt = int(m.group(2))
    for tok in m.group(6).split(","):
        if ":" in tok: parts[fr].setdefault(pt, []).append(tok)

ent = collections.defaultdict(dict)
for fr, pm in parts.items():
    for pt in sorted(pm):
        for tok in pm[pt]:
            p = tok.split(":")
            if len(p) < 5: continue
            ent[fr][(p[0], p[1], p[2])] = (int(p[3]), int(p[4]))

print("=== draws writing the PRESENT target ('P') ===")
pl = sorted({k for fr in ent for k in ent[fr] if k[2] == "P"})
print("%-32s %10s %10s %10s %10s" % ("(vs,fs,P)", "SHARP n", "SHARP inst", "BLUR n", "BLUR inst"))
for key in pl:
    sn = np.mean([ent.get(k, {}).get(key, (0,0))[0] for k in S])
    si = np.mean([ent.get(k, {}).get(key, (0,0))[1] for k in S])
    bn = np.mean([ent.get(k, {}).get(key, (0,0))[0] for k in B])
    bi = np.mean([ent.get(k, {}).get(key, (0,0))[1] for k in B])
    flag = "  <-- differs" if abs(sn-bn) > 0.05 or abs(si-bi) > 0.05 else ""
    print("%-32s %10.2f %10.2f %10.2f %10.2f%s" % (str(key), sn, si, bn, bi, flag))

print("\n=== all entries with a per-frame count difference > 0.05 ===")
def pf(frs):
    a = {}
    n = max(len(frs), 1)
    for f in frs:
        for key, (nn, ii) in ent.get(f, {}).items():
            an, ai = a.get(key, (0.0, 0.0))
            a[key] = (an + nn/n, ai + ii/n)
    return a
as_, ab = pf(S), pf(B)
for key in sorted(set(as_) | set(ab)):
    x = as_.get(key, (0,0)); y = ab.get(key, (0,0))
    if abs(x[0]-y[0]) > 0.05 or abs(x[1]-y[1]) > 0.05:
        print("  %-34s SHARP n=%7.2f inst=%8.2f | BLUR n=%7.2f inst=%8.2f" % (str(key), x[0], x[1], y[0], y[1]))
