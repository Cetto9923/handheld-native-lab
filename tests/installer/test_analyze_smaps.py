#!/usr/bin/env python3
import importlib.util
import json
import subprocess
from pathlib import Path
import tempfile

root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('analyze_smaps',root/'scripts/distribution/analyze_smaps.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
region='''1000-2000 rw-p 00000000 00:00 0 [anon:heap]\nSize: 4 kB\nRss: 4 kB\nPss: 3 kB\nPrivate_Clean: 0 kB\nPrivate_Dirty: 4 kB\nSwap: 0 kB\nSwapPss: 0 kB\nVmFlags: rd wr\n'''
file_region='''2000-4000 r--p 00000000 08:01 17 /path with space/file.so\nSize: 8 kB\nRss: 6 kB\nPss: 2 kB\nPrivate_Clean: 0 kB\nPrivate_Dirty: 0 kB\nSwap: 1 kB\nSwapPss: 1 kB\n'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'s'; p.write_text(region+file_region)
 data=m.summarize(p); assert data['[anon:heap]']['Pss']==3
 assert data['/path with space/file.so']['SwapPss']==1 and len(data)==2
 p.write_text(region+region.replace('1000-2000','4000-5000'))
 assert m.summarize(p)['[anon:heap]']['mappings']==2 and m.summarize(p)['[anon:heap]']['Pss']==6
 for broken in ['', 'Permission denied\n', region.replace('Pss: 3 kB\n',''), region+'Pss: 3 kB\n']:
  p.write_text(broken)
  try: m.summarize(p)
  except ValueError: continue
  raise AssertionError('malformed input accepted')
with tempfile.TemporaryDirectory() as d:
 before=Path(d)/'before'; after=Path(d)/'after'
 before.write_text(region+file_region)
 after.write_text(region.replace('Pss: 3 kB','Pss: 4 kB')+file_region)
 output=subprocess.check_output(['python3',str(root/'scripts/distribution/analyze_smaps.py'),str(before),str(after)],text=True)
 summary=json.loads(output); assert summary['groups'][0]['mapping']=='[anon:heap]' and summary['groups'][0]['delta_kb']['Pss']==1
print('smaps analysis: 8 synthetic cases passed')
