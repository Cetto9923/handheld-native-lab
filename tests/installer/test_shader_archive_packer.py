#!/usr/bin/env python3
"""Exercise actual archive packer with synthetic files in a temporary local directory."""
import hashlib, json, pathlib, struct, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
PACKER=ROOT/'scripts/distribution/build_shader_archive.py'
with tempfile.TemporaryDirectory(prefix='hnl-archive-test-',dir=ROOT/'.local') as tmp:
 p=pathlib.Path(tmp); source=p/'source'; source.mkdir(); idx=p/'index'; idx.mkdir()
 rows=[]
 for key,n,v in [('0000000000000001',40,0x11),('ffffffffffffffff',64,0x22)]:
  f=source/(key+'.xgs'); data=bytes([v])*n; f.write_bytes(data)
  rows.append({'path':'shaders/'+f.name,'source':str(f),'bytes':n,'sha256':hashlib.sha256(data).hexdigest()})
 table=''.join(f"{r['sha256']}\t{r['bytes']}\t{r['path']}\n" for r in rows).encode()
 (idx/'files.tsv').write_bytes(table)
 (idx/'index.json').write_text(json.dumps({'files':rows,'index_sha256':hashlib.sha256(table).hexdigest(),'required':{},'file_count':len(rows),'payload_bytes':sum(r['bytes'] for r in rows)}))
 archive=p/'data.hnla'
 def run(out): return subprocess.run(['python3',str(PACKER),'--index',str(idx),'--out',str(out)],capture_output=True,text=True)
 assert run(archive).returncode==0
 data=archive.read_bytes(); magic,count,res=struct.unpack_from('<8sII',data)
 assert (magic,count,res)==(b'HNLXGS1\0',2,0)
 assert struct.unpack_from('<16sQQ',data,16)==(b'0000000000000001',80,40)
 assert struct.unpack_from('<16sQQ',data,48)==(b'ffffffffffffffff',120,64)
 assert data[80:120]==bytes([0x11])*40 and data[120:]==bytes([0x22])*64
 indexer=ROOT/'scripts/distribution/index_shader_archive.py'
 def reindex(inp,out): return subprocess.run(['python3',str(indexer),'--index',str(idx),'--archive',str(inp),'--out',str(out)],capture_output=True,text=True)
 packed_index=p/'packed-index'; assert reindex(archive,packed_index).returncode==0
 record=json.loads((packed_index/'index.json').read_text()); assert record['file_count']==1 and record['required']['shaders.hnla']==hashlib.sha256(data).hexdigest()
 damaged=p/'damaged.hnla'; damaged.write_bytes(data[:-1]+bytes([data[-1]^1])); assert reindex(damaged,p/'bad-packed-index').returncode!=0
 assert reindex(archive,packed_index).returncode!=0
 before=hashlib.sha256(data).hexdigest(); assert run(archive).returncode!=0 and hashlib.sha256(archive.read_bytes()).hexdigest()==before
 (source/'0000000000000001.xgs').write_bytes(bytes([0x33])*40)
 assert run(p/'corrupt.hnla').returncode!=0
 (idx/'files.tsv').write_bytes(table+b'X')
 assert run(p/'bad-index.hnla').returncode!=0
 print('shader archive packer and indexer: 7 cases passed (synthetic host data)')
