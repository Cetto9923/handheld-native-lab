#!/usr/bin/env python3
"""Host contract tests for the independent archive reader; no game inputs."""
import os, pathlib, struct, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
 p=pathlib.Path(tmp); binary=p/'check'
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-pthread',str(root/'tests/installer/shader_archive_contract.cpp'),'-o',str(binary)],check=True)
 header=b'HNLXGS1\0'+struct.pack('<II',2,0)
 index=struct.pack('<16sQQ',b'0000000000000001',80,40)+struct.pack('<16sQQ',b'ffffffffffffffff',120,64)
 raw=header+index+b'\x11'*40+b'\x22'*64
 def check(name,data,mode='reject'):
  f=p/name; f.write_bytes(data); subprocess.run([str(binary),str(f),mode],check=True)
 check('ok',raw,'valid')
 check('truncate',raw,'truncate')
 check('bad_magic',b'X'+raw[1:])
 check('short',raw[:-1])
 check('trailing',raw+b'x')
 check('reserved',raw[:12]+struct.pack('<I',1)+raw[16:])
 check('empty',raw[:8]+struct.pack('<I',0)+raw[12:])
 check('huge_count',raw[:8]+struct.pack('<I',1000001)+raw[12:])
 check('duplicate',raw[:48]+raw[16:32]+raw[64:])
 check('bad_key',raw[:16]+b'Z'+raw[17:])
 check('overlap',raw[:32]+struct.pack('<Q',79)+raw[40:])
 check('tiny_entry',raw[:40]+struct.pack('<Q',31)+raw[48:])
 (p/'link').symlink_to(p/'ok')
 subprocess.run([str(binary),str(p/'link'),'reject'],check=True)
 print('shader archive contract: 13 cases passed (synthetic host data; concurrent readers included)')
