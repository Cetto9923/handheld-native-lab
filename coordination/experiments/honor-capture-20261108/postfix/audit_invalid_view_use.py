from pathlib import Path
import json,mmap,struct,ctypes
P=Path(__file__).resolve().parent;U=lambda b,o:struct.unpack_from('<I',b,o)[0];Q=lambda b,o:struct.unpack_from('<Q',b,o)[0]
L=ctypes.CDLL('/opt/homebrew/lib/libzstd.dylib');L.ZSTD_decompress.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t];L.ZSTD_decompress.restype=ctypes.c_size_t
def raw(body,k):
 if k&0x80000000:
  n=Q(body,12);d=ctypes.create_string_buffer(max(n,1));assert L.ZSTD_decompress(d,n,body[20:],len(body)-20)==n;return d.raw[:n]
 return body[12:]
def submit(b):
 n=U(b,8);p=12;tag=U(b,p);assert tag in (226,228);p+=12
 if tag==228:assert Q(b,p)==n;p+=8
 def arr(p,n,fmt):
  tag=U(b,p);p+=4
  if tag==5:assert n==0;return [],p
  assert tag in (196,228);p+=8;assert Q(b,p)==n;p+=8
  sz=struct.calcsize(fmt);vals=list(struct.unpack_from('<'+fmt*n,b,p));return vals,p+sz*n
 out=[]
 for _ in range(n):
  assert U(b,p)==4 and U(b,p+4)==35;p+=8
  nw=U(b,p);p+=4;ws,p=arr(p,nw,'Q');st,p=arr(p,nw,'I')
  nc=U(b,p);p+=4;cbs,p=arr(p,nc,'Q')
  ns=U(b,p);p+=4;sigs,p=arr(p,ns,'Q');out+=cbs
 assert p+12==len(b) and U(b,p+8)==0
 return out

invalid_reports=json.load(open(P/'captured-R32-view-ranges.json'));result=[]
for ir in invalid_reports:
 family=ir['family'];bad={r['view']for r in ir['R32_range_not_multiple4']};sets={};bound={};events={};beg={};uses=[];state=True
 with(P/('fixed-'+family+'.gfxr')).open('rb')as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)as m:
  pos=16+8*U(m,12);i=0
  while pos<len(m):
   n,k=struct.unpack_from('<QI',m,pos);kind=k&0x7fffffff
   if kind==2 and U(m,pos+12)==2:state=False
   elif kind==4:
    api=U(m,pos+12)
    if api in(69792,69735,69722,69738,69739,69740,69741,69742,69743,69650):
     b=raw(m[pos+12:pos+12+n],k)
     if api==69792:
      ni,nb,nv=struct.unpack_from('<3Q',b,36);assert len(b)==60+ni*20+nb*24+nv*8;at=60+ni*20+nb*24;views={Q(b,at+8*x)for x in range(nv)};sets[Q(b,8)]=views&bad
     elif api==69722:cb=Q(b,0);bound[cb]={};events[cb]=[];beg[cb]=i
     elif api==69735:
      cb=Q(b,0);point=U(b,8);ns=U(b,24);assert Q(b,40)==ns;viewset=set()
      for t in range(ns):viewset|=sets.get(Q(b,48+8*t),set())
      bound.setdefault(cb,{})[point]=viewset
     elif api in(69738,69739,69740,69741,69742,69743):
      cb=Q(b,0);point=1 if api in(69742,69743)else 0;v=bound.get(cb,{}).get(point,set())
      if v:events.setdefault(cb,[]).append(dict(draw=i,api=api,cb=cb,begin=beg.get(cb),bad_views=sorted(v),stateRestore=state))
     elif api==69650 and not state:
      for cb in submit(b):
       for e in events.get(cb,[]):uses.append(dict(e,submit=i))
   pos+=12+n;i+=1
 result.append(dict(family=family,invalid_view_ids=sorted(bad),actual_submitted_bad_binding_commands=uses))
(P/'invalid-view-live-use.json').write_text(json.dumps(result,indent=2));print(json.dumps([dict(family=x['family'],invalidViews=len(x['invalid_view_ids']),submitted_bad_binding_commands=len(x['actual_submitted_bad_binding_commands']))for x in result],indent=2))
