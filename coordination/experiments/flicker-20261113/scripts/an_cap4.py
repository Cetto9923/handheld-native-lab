
# -*- coding: utf-8 -*-
import numpy as np, os, glob, sys
DIR = sys.argv[1] if len(sys.argv)>1 else "/tmp/ffrs-flicker-csv/cap4"
def load(p):
    raw=open(p,"rb").read()
    i=raw.index(b"\n"); hdr=raw[:i].decode("latin1"); body=raw[i+1:]
    d=dict(kv.split("=",1) for kv in hdr.split() if "=" in kv)
    w=int(d["width"]); h=int(d["height"]); bpp=int(d["bpp"])
    return hdr, np.frombuffer(body[:w*h*bpp],dtype=np.uint8).reshape(h,w,bpp)
def lum(a):
    return (a[:,:,:3].astype(np.float32)*np.array([0.299,0.587,0.114],dtype=np.float32)).sum(2)
def lapvar(r):
    k=r[1:-1,1:-1]*-4+r[:-2,1:-1]+r[2:,1:-1]+r[1:-1,:-2]+r[1:-1,2:]
    return float(k.var())
ROI=(260,560,400,540)   # pure cobblestone, protagonist excluded
ps=sorted(glob.glob(os.path.join(DIR,"*.bin")))
if not ps:
    ps=sorted(glob.glob(os.path.join(DIR,"frame-dump","*.bin")))
print("frames:",len(ps))
L={}; LUM={}
for p in ps:
    hdr,a=load(p)
    present=int(hdr.split("present=")[1].split()[0])
    l=lum(a); LUM[present]=l
    x0,x1,y0,y1=ROI
    r=l[y0:y1,x0:x1]
    L[present]=(float(r.mean()), lapvar(r))
ks=sorted(L)
print("present  mean   lapvar")
for k in ks:
    print("%7d  %6.2f  %8.2f"%(k,L[k][0],L[k][1]))
ms=[L[k][0] for k in ks]; ss=[L[k][1] for k in ks]
print("mean range=%.2f  lapvar range=%.2f"%(max(ms)-min(ms), max(ss)-min(ss)))
# classify by sharpness median
med=np.median(ss)
state={k:("BLUR" if L[k][1]<med else "SHARP") for k in ks}
print("states:", " ".join("%d:%s"%(k,state[k]) for k in ks))
# diff maps for adjacent cross-state pairs
CH=" .:-=+*#%@"
def ascii_map(x0,x1,y0,y1):
    lines=[]
    for y in range(y0,y1,10):
        row=""
        for x in range(x0,x1,6):
            v=float(abs(diff[y,x]))
            idx=min(len(CH)-1,int(v/4.0))
            row+=CH[idx]
        lines.append(row)
    return lines
pairs=[]
for i in range(len(ks)-1):
    if ks[i+1]-ks[i]==1 and state[ks[i]]!=state[ks[i+1]]:
        pairs.append((ks[i],ks[i+1]))
print("cross-state adjacent pairs:",pairs)
if pairs:
    p1,p2=pairs[0]
    diff=LUM[p2]-LUM[p1]
    x0,x1,y0,y1=ROI
    print("pair %d->%d  diff min=%.1f max=%.1f"%(p1,p2,diff[y0:y1,x0:x1].min(),diff[y0:y1,x0:x1].max()))
    print("ASCII |diff| (rows y=%d..%d step10, cols x=%d..%d step6):"%(y0,y1,x0,x1))
    for ln in ascii_map(x0,x1,y0,y1): print("  "+ln)
    # spatial extent of strong diff over whole image
    strong=(np.abs(diff)>3)
    ys,xs=np.nonzero(strong)
    if len(xs): print("whole-image |diff|>3 count=%d  x[%d..%d] y[%d..%d]"%(len(xs),xs.min(),xs.max(),ys.min(),ys.max()))
    # stability across pairs: IoU of strong masks
    for q1,q2 in pairs[1:]:
        d2=np.abs(LUM[q2]-LUM[q1])>3
        inter=(strong&d2).sum(); uni=(strong|d2).sum()
        print("  IoU(%d,%d) vs (%d,%d) = %.3f"%(p1,p2,q1,q2,inter/max(uni,1)))
