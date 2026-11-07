from pathlib import Path
import mmap,struct,ctypes,json,collections,datetime
P=Path(__file__).resolve().parent
L=ctypes.CDLL('/opt/homebrew/lib/libzstd.dylib')
L.ZSTD_getFrameContentSize.argtypes=[ctypes.c_void_p,ctypes.c_size_t];L.ZSTD_getFrameContentSize.restype=ctypes.c_ulonglong
L.ZSTD_decompress.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t];L.ZSTD_decompress.restype=ctypes.c_size_t
L.ZSTD_findFrameCompressedSize.argtypes=[ctypes.c_void_p,ctypes.c_size_t];L.ZSTD_findFrameCompressedSize.restype=ctypes.c_size_t
L.ZSTD_isError.argtypes=[ctypes.c_size_t];L.ZSTD_isError.restype=ctypes.c_uint
c=collections.Counter(); offsets=collections.Counter();total=0;maximum=0
with (P/'title-stream.gfxr').open('rb')as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)as m:
 pos=16+8*struct.unpack_from('<I',m,12)[0]
 while pos<len(m):
  size,kind=struct.unpack_from('<QI',m,pos);assert pos+12+size<=len(m)
  if kind&0x80000000:
   b=m[pos+12:pos+12+size];magic=bytes.fromhex('28b52ffd');candidates=[];start=0
   while True:
    o=b.find(magic,start)
    if o<0:break
    data=b[o:];n=L.ZSTD_getFrameContentSize(data,len(data));sz=L.ZSTD_findFrameCompressedSize(data,len(data))
    if n<2**32 and not L.ZSTD_isError(sz) and sz==len(data):candidates.append((o,n))
    start=o+1
   assert len(candidates)==1,(pos,kind,candidates)
   o,n=candidates[0];data=b[o:]
   if kind&0x7fffffff==4:assert o==20 and struct.unpack_from('<Q',b,12)[0]==n
   out=ctypes.create_string_buffer(max(n,1));r=L.ZSTD_decompress(out,n,data,len(data));assert not L.ZSTD_isError(r) and r==n,(pos,n,r)
   c[str(kind&0x7fffffff)]+=1;offsets[str((kind&0x7fffffff,o))]+=1;total+=n;maximum=max(maximum,n)
  pos+=12+size
 assert pos==len(m)
s={'time':datetime.datetime.now().astimezone().isoformat(),'all_compressed_blocks_ZSTD_decoded':True,'by_base_kind':dict(c),'payload_offsets':dict(offsets),'uncompressed_bytes':total,'maximum_block_bytes':maximum,'exact_eof':True,'scope':'Transport/compression title control only; not effect capture completeness or shader semantic acceptance'}
(P/'zstd-block-audit.json').write_text(json.dumps(s,indent=2)+'\n');print(json.dumps(s,indent=2))
