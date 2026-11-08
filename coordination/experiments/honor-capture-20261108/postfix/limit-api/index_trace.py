from pathlib import Path
import struct,mmap,ctypes,json,hashlib
P=Path(__file__).resolve().parent
U=lambda b,o:struct.unpack_from('<I',b,o)[0]
Q=lambda b,o:struct.unpack_from('<Q',b,o)[0]
L=ctypes.CDLL('/opt/homebrew/lib/libzstd.dylib');L.ZSTD_decompress.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t];L.ZSTD_decompress.restype=ctypes.c_size_t
def raw(body,kind):
 if kind&0x80000000:
  n=Q(body,12);d=ctypes.create_string_buffer(max(n,1));assert L.ZSTD_decompress(d,n,body[20:],len(body)-20)==n;return d.raw[:n]
 return body[12:]
def pipeline(b):
 assert U(b,16)==1
 # Stage structs are identified by sType, null pNext marker, and legal stage bit.
 st=[]
 for at in range(len(b)-24):
  if b[at:at+8]==struct.pack('<II',18,35):
   flags,stage,module=struct.unpack_from('<IIQ',b,at+8)
   assert flags==0 and stage in (1,2,4,8,16,32)
   st.append({'stage':stage,'module':module})
 assert len(st)==U(b,52)
 return {'pipeline':Q(b,len(b)-12),'stages':st}
c=json.loads((P/'api-known-control.json').read_text())['vkCreateGraphicsPipelines'];v=pipeline(bytes.fromhex(c['rawhex']));a=c['known']['function']['args'];assert v['pipeline']==a['pPipelines'][0] and [x['module'] for x in v['stages']]==[x['module'] for x in a['pCreateInfos'][0]['pStages']]
shaders={};pipes={};events=[];special=[]
keep={69691,69697,69725,69738,69739,69741,69765,69767,69726,69727,69781,69722,69720,69650}
with (P.parent/'fixed-limit.gfxr').open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as m,(P/'compact-events.jsonl').open('w') as out:
 pos=16+8*U(m,12);i=0
 while pos<len(m):
  size,kind=struct.unpack_from('<QI',m,pos);assert pos+12+size<=len(m)
  k=kind&0x7fffffff
  if k in (1,2):
   r={'index':i,'kind':k,'rawhex':m[pos+12:pos+12+size].hex()};out.write(json.dumps(r)+'\n');special.append(r)
  elif k==4:
   api=U(m,pos+12)
   if api in keep:
    b=raw(m[pos+12:pos+12+size],kind);r={'index':i,'api':api}
    if api==69691:
     n=Q(b,32);assert len(b)==n+88 and Q(b,52)*4==n and U(b,60)==0x07230203 and U(b,len(b)-4)==0
     spv=b[60:60+n];sid=Q(b,len(b)-12);w=struct.unpack('<'+'I'*(n//4),spv);at=5;entry=[]
     while at<len(w):
      wc=w[at]>>16;op=w[at]&65535;assert wc and at+wc<=len(w)
      if op==15:entry.append(w[at+1])
      at+=wc
     assert at==len(w)
     (P/'actual-shaders'/('sh'+str(sid))).write_bytes(spv);shaders[sid]={'module':sid,'created':i,'bytes':n,'sha256':hashlib.sha256(spv).hexdigest(),'execution_models':entry};r.update(shaders[sid])
    elif api==69697:
     v=pipeline(b);pipes[v['pipeline']]=dict(v,created=i,rawhex=b.hex());r.update(v)
    else:r['rawhex']=b.hex()
    out.write(json.dumps(r)+'\n')
  pos+=12+size;i+=1
(P/'shader-manifest.json').write_text(json.dumps(list(shaders.values()),indent=2));(P/'pipeline-manifest.json').write_text(json.dumps(list(pipes.values()),indent=2));(P/'framing-summary.json').write_text(json.dumps({'bytes':pos,'blocks':i,'exact_eof':True,'boundaries':special},indent=2));print('actual modules',len(shaders),'pipelines',len(pipes),'boundaries',special)
