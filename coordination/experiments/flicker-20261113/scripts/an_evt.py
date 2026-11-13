# -*- coding: utf-8 -*-
"""Frame-level alignment of the two candidate present-target draws.

Candidates from the census:
  17005546:5e2affe7:P  and  17005546:82bc33fa:P   more often in BLUR
  e3cab147:b48a9e83:P                             more often in SHARP

For every captured frame we check whether each candidate was drawn, then
cross-tabulate against the pixel state. A candidate that appears in exactly
one state is the paint that carries the veil.
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

CANDS = [("17005546","5e2affe7","P"), ("17005546","82bc33fa","P"), ("e3cab147","b48a9e83","P"),
         ("17005546","abe4a0ce","P"), ("17005546","d4df8c48","P"), ("17005546","2fdeb4d9","P"),
         ("fae0b835","4746d49d","P"), ("fae0b835","4e81f732","P"), ("fae0b835","73d0ef2f","P")]

print("%7s %-6s %s" % ("present", "state", "  ".join("%s:%s" % (c[1][:8], c[2]) for c in CANDS)))
for k in ks:
    row = []
    for c in CANDS:
        v = ent.get(k, {}).get(c, (0, 0))[0]
        row.append("%8d" % v)
    print("%7d %-6s %s" % (k, state[k], "  ".join(row)))

print("\n=== cross-tabulation: presence vs state ===")
for c in CANDS:
    tab = collections.Counter()
    for k in ks:
        present = ent.get(k, {}).get(c, (0,0))[0] > 0
        tab[(state[k], present)] += 1
    print("  %-34s  SHARP+drawn=%d  SHARP+not=%d  BLUR+drawn=%d  BLUR+not=%d"
          % (str(c), tab[("SHARP",True)], tab[("SHARP",False)], tab[("BLUR",True)], tab[("BLUR",False)]))

print("\n=== per-frame instance counts for the two 17005546 candidates ===")
for c in CANDS[:5]:
    sv = [ent.get(k, {}).get(c, (0,0))[1] for k in ks if state[k]=="SHARP"]
    bv = [ent.get(k, {}).get(c, (0,0))[1] for k in ks if state[k]=="BLUR"]
    if sv and bv:
        print("  %-34s SHARP inst mean=%.2f  BLUR inst mean=%.2f" % (str(c), np.mean(sv), np.mean(bv)))
