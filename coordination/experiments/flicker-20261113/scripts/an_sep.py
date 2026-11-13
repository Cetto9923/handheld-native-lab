# -*- coding: utf-8 -*-
"""Categorical separation test over EVERY engine log field.

Earlier numeric correlation skipped the hex fields (texhash, rthash, c5a...).
Here every field of every FLAME_* line is treated as a categorical value and
we ask: does any field's value set separate the SHARP frames from the BLUR
frames? A field with zero overlap would be the smoking gun.
"""
import numpy as np, os, glob, sys, re, collections, zlib, struct

FRAME_DIR = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ffrs-flicker-csv/more"
LOG      = sys.argv[2] if len(sys.argv) > 2 else "/tmp/ffrs-flicker-csv/engine7.log"
OUT      = sys.argv[3] if len(sys.argv) > 3 else "/tmp/ffrs-flicker-csv/look"

def load(p):
    raw = open(p, "rb").read()
    i = raw.index(b"\n"); hdr = raw[:i].decode("latin1"); body = raw[i+1:]
    d = dict(kv.split("=", 1) for kv in hdr.split() if "=" in kv)
    w = int(d["width"]); h = int(d["height"]); bpp = int(d["bpp"])
    return hdr, np.frombuffer(body[:w*h*bpp], dtype=np.uint8).reshape(h, w, bpp)

def lum(a):
    return (a[:, :, :3].astype(np.float32) * np.array([0.299,0.587,0.114], dtype=np.float32)).sum(2)

def write_png(path, rgb):
    h, w, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(h))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag+data) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))

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
S = [k for k in ks if state[k] == "SHARP"]
B = [k for k in ks if state[k] == "BLUR"]

log = open(LOG, "r", errors="replace").read()

def test_fields(line_re, label, keep_frames=None):
    frames_of = collections.defaultdict(list)
    for m in re.finditer(line_re, log):
        fr = int(m.group(1))
        if keep_frames and fr not in keep_frames: continue
        for tok in m.group(2).split():
            if "=" not in tok: continue
            k, v = tok.split("=", 1)
            frames_of[fr].append((label + "." + k, v))
    # build per-frame dict
    per = {fr: dict(pairs) for fr, pairs in frames_of.items()}
    allkeys = set()
    for d in per.values(): allkeys |= set(d)
    print("\n=== %s : fields that separate SHARP from BLUR ===" % label)
    for f in sorted(allkeys):
        sv = {per[k].get(f) for k in S if k in per}
        bv = {per[k].get(f) for k in B if k in per}
        sv.discard(None); bv.discard(None)
        if not sv or not bv: continue
        if not (sv & bv):
            print("  *** PERFECT SEPARATION %-28s SHARP=%s  BLUR=%s" % (f, sorted(sv)[:3], sorted(bv)[:3]))
        elif len(sv) == 1 and len(bv) == 1:
            pass
        else:
            # partial: how many SHARP frames carry a BLUR-only value
            bonly = bv - sv
            if bonly:
                hit = sum(1 for k in S if per[k].get(f) in bonly)
                print("  ~   %-28s SHARPvalues=%d BLURvalues=%d  SHARPframesCarryingBLURvalue=%d/%d"
                      % (f, len(sv), len(bv), hit, len(S)))

test_fields(r"FLAME_FRAME fr=(\d+) (.*)", "FF", set(ks))
test_fields(r"FLAME_Z fr=(\d+) (.*)", "FZ", set(ks))
test_fields(r"FLAME_DR fr=(\d+) part=\d+ pmem=\S+ pf=\d+ over=\d+ (.*)", "DR", set(ks))
test_fields(r"FLAME_EV fr=(\d+) part=\d+ over=\d+ (.*)", "EV", set(ks))

# ---------- zoom crops of the ground left of the protagonist ----------
os.makedirs(OUT, exist_ok=True)
b1 = batches[0]
sh = [k for k in b1 if state[k] == "SHARP"][0]
bl = [k for k in b1 if state[k] == "BLUR"][0]
def crop(k, box):
    x0,x1,y0,y1 = box
    return F[k][y0:y1, x0:x1, :3]
def up(a, f=2):
    return np.repeat(np.repeat(a, f, axis=0), f, axis=1)
BOX = (150, 700, 330, 620)
ca, cb = crop(sh, BOX), crop(bl, BOX)
sep = np.full((ca.shape[0], 6, 3), 255, np.uint8)
write_png(OUT + "/f_ground_zoom_sharp.png", up(ca))
write_png(OUT + "/g_ground_zoom_blur.png", up(cb))
write_png(OUT + "/h_ground_zoom_sbs.png", np.concatenate([up(ca), np.full((up(ca).shape[0], 6, 3), 255, np.uint8), up(cb)], axis=1))
d = np.abs(cb.astype(np.int16) - ca.astype(np.int16)).max(2)
write_png(OUT + "/i_ground_zoom_diff_x8.png", up(np.clip(d.astype(np.float32)*8,0,255).astype(np.uint8)[...,None].repeat(3,2)))
print("\nwrote ground zoom PNGs (sharp=%d blur=%d)" % (sh, bl))
