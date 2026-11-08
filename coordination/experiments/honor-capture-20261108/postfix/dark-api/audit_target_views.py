from pathlib import Path
import mmap,struct,ctypes,json,hashlib
P=Path(__file__).resolve().parent;U=lambda b,o:struct.unpack_from('<I',b,o)[0];Q=lambda b,o:struct.unpack_from('<Q',b,o)[0]
L=ctypes.CDLL('/opt/homebrew/lib/libzstd.dylib');L.ZSTD_decompress.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t];L.ZSTD_decompress.restype=ctypes.c_size_t
def raw(body,k):
 if k&0x80000000:
  n=Q(body,12);d=ctypes.create_string_buffer(max(n,1));assert L.ZSTD_decompress(d,n,body[20:],len(body)-20)==n;return d.raw[:n]
 return body[12:]
def view(b):
 assert len(b)==88 and U(b,20)==13 and U(b,24)==35 and U(b,84)==0
 return dict(view=Q(b,76),buffer=Q(b,32),format=U(b,40),offset=Q(b,44),range=Q(b,52))
def template(b):
 n=U(b,32);assert len(b)==112+32*n and Q(b,48)==n
 return {'template':Q(b,len(b)-12),'entries':[dict(zip(['binding','element','count','type','offset','stride'],struct.unpack_from('<4I2Q',b,56+32*i))) for i in range(n)]}
def update(b):
 ni,nb,nv=struct.unpack_from('<3Q',b,36);assert len(b)==60+ni*20+nb*24+nv*8;at=60
 ims=[dict(zip(['sampler','imageView','layout'],struct.unpack_from('<QQI',b,at+20*i))) for i in range(ni)];at+=20*ni
 bufs=[dict(zip(['buffer','offset','range'],struct.unpack_from('<3Q',b,at+24*i))) for i in range(nb)];at+=24*nb
 return {'set':Q(b,8),'template':Q(b,16),'images':ims,'buffers':bufs,'views':[Q(b,at+8*i) for i in range(nv)]}
def bind(b):
 ns=U(b,24);assert Q(b,40)==ns;at=48+ns*8;nd=U(b,at);assert Q(b,at+16)==nd and len(b)==at+24+4*nd
 return {'cb':Q(b,0),'layout':Q(b,12),'sets':[Q(b,48+i*8) for i in range(ns)],'dynamic':list(struct.unpack_from('<'+'I'*nd,b,at+24))}
ts={};vs={};ups={};binds={};outputs=[];discovered=json.load(open(P/'discovered-targets.json'));wanted={r['index']for r in discovered['selected']}
with (P.parent/'fixed-dark.gfxr').open('rb')as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)as m:
 pos=16+8*U(m,12);i=0
 while i<=max(wanted):
  size,k=struct.unpack_from('<QI',m,pos)
  if k&0x7fffffff==4:
   api=U(m,pos+12)
   if api in (69684,69790,69792,69735)or i in wanted:
    b=raw(m[pos+12:pos+12+size],k)
    if api==69684:r=view(b);vs[r['view']]=dict(r,created=i)
    elif api==69790:r=template(b);ts[r['template']]=dict(r,created=i)
    elif api==69792:r=update(b);ups[r['set']]=dict(r,updated=i)
    elif api==69735 and U(b,8)==0:r=bind(b);binds[r['cb']]=dict(r,bound=i)
    if i in wanted:
     bi=binds[Q(b,0)];assert len(bi['sets'])==1;up=ups[bi['sets'][0]];assert up['updated']<i;te=ts[up['template']];a=c=v=0;rows=[]
     for e in te['entries']:
      assert e['count']==1;r=dict(e)
      if e['type']in(4,5):r.update(vs[up['views'][v]]);v+=1
      elif e['type']in(6,7,8,9):r.update(up['buffers'][c]);c+=1
      else:r.update(up['images'][a]);a+=1
      rows.append(r)
     dyn=sorted([r for r in rows if r['type']in(8,9)],key=lambda r:(r['binding'],r['element']));assert len(dyn)==len(bi['dynamic'])
     for r,o in zip(dyn,bi['dynamic']):r['dynamic_offset']=o;r['effective_offset']=r['offset']+o
     outputs.append({'draw':i,'frame':discovered['native_frame'],'bind':bi,'updated':up['updated'],'template':te['template'],'rows':rows})
  pos+=12+size;i+=1
assert len(outputs)==2
(P/'target-resource-matrices.json').write_text(json.dumps(outputs,indent=2));print([(r['draw'],len(r['rows']))for r in outputs])
