from pathlib import Path
import json,mmap,struct,ctypes
P=Path(__file__).resolve().parent;U=lambda b,o:struct.unpack_from('<I',b,o)[0];Q=lambda b,o:struct.unpack_from('<Q',b,o)[0]
L=ctypes.CDLL('/opt/homebrew/lib/liblz4.dylib');L.LZ4_decompress_safe.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
def raw(body,k):
 if k&0x80000000:
  n=Q(body,12);d=ctypes.create_string_buffer(n);assert L.LZ4_decompress_safe(body[20:],d,len(body)-20,n)==n;return d.raw
 return body[12:]
report=[]
for family in ['dark','limit']:
 invalid=[];views=0;counts={}
 with(P/('original-'+family+'.gfxr')).open('rb')as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)as m:
  pos=16+8*U(m,12);i=0
  while pos<len(m):
   n,k=struct.unpack_from('<QI',m,pos)
   if k&0x7fffffff==4 and U(m,pos+12)==69684:
    b=raw(m[pos+12:pos+12+n],k);assert len(b)==88 and U(b,20)==13 and U(b,24)==35
    fmt,offset,size=U(b,40),Q(b,44),Q(b,52);views+=1;counts[fmt]=counts.get(fmt,0)+1
    if fmt in(98,99,100)and size!=0xffffffffffffffff and size%4:invalid.append(dict(index=i,format=fmt,offset=offset,range=size,view=Q(b,76),buffer=Q(b,32)))
   pos+=12+n;i+=1
 report.append(dict(family=family,view_creations=views,formats=counts,R32_range_not_multiple4=invalid))
(P/'captured-R32-view-ranges.json').write_text(json.dumps(report,indent=2));print(json.dumps([dict(family=r['family'],views=r['view_creations'],invalid_R32_count=len(r['R32_range_not_multiple4']))for r in report],indent=2))
