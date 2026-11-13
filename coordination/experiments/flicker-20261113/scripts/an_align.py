# -*- coding: utf-8 -*-
"""Align engine-presented pixels (SHARP/BLUR) with the engine's per-frame draw census.

For every present number we have:
  - from an_cap4-style analysis: mean/lapvar of the pure-cobblestone ROI
  - from the engine log: FLAME_FRAME (aggregates) and FLAME_DR (full (vs,fs,target)->n/inst list)

The BLUR state is the bright/flat one (BLUR = 0.9265*SHARP + 17.149), so:
  BLUR = high mean  AND low lapvar
  SHARP = low mean  AND high lapvar
"""
import numpy as np, os, glob, sys, re, collections

FRAME_DIR = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ffrs-flicker-csv/now2"
LOG      = sys.argv[2] if len(sys.argv) > 2 else "/tmp/ffrs-flicker-csv/engine6.log"
ROI      = (260, 560, 400, 540)

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

# ---- pixel side -------------------------------------------------------
frames = {}
for p in sorted(glob.glob(os.path.join(FRAME_DIR, "*.bin"))):
    hdr, a = load(p)
    present = int(hdr.split("present=")[1].split()[0])
    l = lum(a)
    x0, x1, y0, y1 = ROI
    frames[present] = {"mean": float(l[y0:y1, x0:x1].mean()), "lap": lapvar(l[y0:y1, x0:x1]),
                       "lum": l}
if not frames:
    print("no frames"); sys.exit(1)

means = [v["mean"] for v in frames.values()]
laps  = [v["lap"]  for v in frames.values()]
mmed, lmed = np.median(means), np.median(laps)
state = {}
for k, v in frames.items():
    # bright + flat => BLUR ; dim + sharp => SHARP
    state[k] = "BLUR" if (v["mean"] > mmed and v["lap"] < lmed) else "SHARP"

print("=== pixel states ===")
for k in sorted(frames):
    print("  %6d  %-5s mean=%7.2f lap=%8.2f" % (k, state[k], frames[k]["mean"], frames[k]["lap"]))

# ---- engine log side --------------------------------------------------
log = open(LOG, "r", errors="replace").read()
frame_re = re.compile(r"FLAME_FRAME fr=(\d+) (.*)")
dr_re    = re.compile(r"FLAME_DR fr=(\d+) part=(\d+) pmem=(\S+) pf=(\d+) over=(\d+) (.*)")
ev_re    = re.compile(r"FLAME_EV fr=(\d+) part=(\d+) over=(\d+) (.*)")

eng_frame = {}
for m in frame_re.finditer(log):
    eng_frame[int(m.group(1))] = m.group(2)

dr = collections.defaultdict(dict)   # present -> entry -> value
dr_over = {}
for m in dr_re.finditer(log):
    fr = int(m.group(1))
    dr_over[fr] = max(dr_over.get(fr, 0), int(m.group(5)))
    for tok in m.group(6).split(","):
        if not tok or ":" not in tok: continue
        parts = tok.split(":")
        if len(parts) < 5: continue
        vs, fs, tgt, n, inst = parts[0], parts[1], parts[2], parts[3], parts[4]
        dr[fr][(vs, fs, tgt)] = (int(n), int(inst))

ev = collections.defaultdict(dict)
for m in ev_re.finditer(log):
    fr = int(m.group(1))
    for tok in m.group(4).split(","):
        if not tok or ":" not in tok: continue
        p = tok.split(":")
        if len(p) < 4: continue
        ev[fr][(p[0], p[1], p[2])] = int(p[3])

print("\n=== census coverage for our frames ===")
for k in sorted(frames):
    print("  %6d  state=%-5s draw_entries=%3d  over=%3d  ev_entries=%3d  eng_frame=%s"
          % (k, state[k], len(dr.get(k, {})), dr_over.get(k, -1), len(ev.get(k, {})), "yes" if k in eng_frame else "NO"))

# ---- SHARP vs BLUR comparison ----------------------------------------
sharp = [k for k in sorted(frames) if state[k] == "SHARP"]
blur  = [k for k in sorted(frames) if state[k] == "BLUR"]
print("\nSHARP frames:", sharp)
print("BLUR  frames:", blur)

def union_entries(frs):
    u = {}
    for f in frs:
        for key, val in dr.get(f, {}).items():
            u.setdefault(key, []).append((f, val))
    return u

print("\n=== draw entries: present in SHARP but not in BLUR (or vice versa) ===")
us, ub = union_entries(sharp), union_entries(blur)
only_s = sorted(set(us) - set(ub))
only_b = sorted(set(ub) - set(us))
print("only in SHARP (%d):" % len(only_s))
for key in only_s: print("   ", key, us[key])
print("only in BLUR (%d):" % len(only_b))
for key in only_b: print("   ", key, ub[key])

print("\n=== draw entries with differing n/inst between states (aggregate by state) ===")
def agg(frs):
    a = {}
    for f in frs:
        for key, (n, inst) in dr.get(f, {}).items():
            an, ai = a.get(key, (0, 0))
            a[key] = (an + n, ai + inst)
    return a
as_, ab = agg(sharp), agg(blur)
diffs = []
for key in sorted(set(as_) | set(ab)):
    sn, si = as_.get(key, (0, 0))
    bn, bi = ab.get(key, (0, 0))
    if (sn, si) != (bn, bi):
        diffs.append((key, (sn, si), (bn, bi)))
print("differing entries: %d of %d" % (len(diffs), len(set(as_) | set(ab))))
for key, s, b in diffs:
    print("   %-40s SHARP n=%-4d inst=%-6d | BLUR n=%-4d inst=%-6d" % (str(key), s[0], s[1], b[0], b[1]))

print("\n=== event (8b27 target) differences ===")
se, be = union_entries(sharp), union_entries(blur)
d2 = []
for key in sorted(set(ev.get(sharp[0], {})) | set(ev.get(blur[0], {})) if sharp and blur else []):
    pass
for f in sharp:
    for key, n in ev.get(f, {}).items():
        pass
agg_ev = {}
for grp, frs in (("SHARP", sharp), ("BLUR", blur)):
    a = {}
    for f in frs:
        for key, n in ev.get(f, {}).items():
            a[key] = a.get(key, 0) + n
    agg_ev[grp] = a
allk = sorted(set(agg_ev["SHARP"]) | set(agg_ev["BLUR"]))
for key in allk:
    s = agg_ev["SHARP"].get(key, 0); b = agg_ev["BLUR"].get(key, 0)
    if s != b:
        print("   %-40s SHARP=%-6d BLUR=%-6d" % (str(key), s, b))
if not any(agg_ev["SHARP"].get(k,0) != agg_ev["BLUR"].get(k,0) for k in allk):
    print("   (no event differences)")

print("\n=== FLAME_FRAME fields that differ between states ===")
if sharp and blur:
    fs_ = eng_frame.get(sharp[0], ""); fb_ = eng_frame.get(blur[0], "")
    d1 = dict(kv.split("=",1) for kv in fs_.split() if "=" in kv)
    d2 = dict(kv.split("=",1) for kv in fb_.split() if "=" in kv)
    for k in sorted(set(d1) | set(d2)):
        if d1.get(k) != d2.get(k):
            print("   %-12s SHARP=%-14s BLUR=%-14s" % (k, d1.get(k), d2.get(k)))
