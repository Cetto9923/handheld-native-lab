#!/usr/bin/env python3
"""Create a new trusted data index after independently validating an XGS archive."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from native_data_bundle import ROOT


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--index', type=Path, required=True)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    local=(ROOT/'.local').resolve(); source=a.index.resolve(); archive=a.archive.resolve(); out=a.out.resolve()
    if a.archive.is_symlink() or not source.is_relative_to(local) or not archive.is_relative_to(local) or not out.is_relative_to(local) or out.is_relative_to(local/'baselines') or out.exists():
        ap.error('use new local paths outside frozen baselines')
    old=json.loads((source/'index.json').read_text()); rows=old['files']; raw=(source/'files.tsv').read_bytes()
    expected=''.join(f"{r['sha256']}\t{r['bytes']}\t{r['path']}\n" for r in rows).encode()
    if raw!=expected or hashlib.sha256(raw).hexdigest()!=old['index_sha256']:
        raise ValueError('original index mismatch')
    shaders=[r for r in rows if r['path'].startswith('shaders/')]
    h=hashlib.sha256(); total=archive.stat().st_size
    if total>64*1024**3: raise ValueError('archive exceeds size limit')
    with archive.open('rb') as f:
        header=f.read(16); h.update(header)
        if len(header)!=16: raise ValueError('short header')
        magic,count,reserved=struct.unpack('<8sII',header)
        if magic!=b'HNLXGS1\0' or count!=len(shaders) or not count or count>1000000 or reserved: raise ValueError('header mismatch')
        table=f.read(32*count); h.update(table)
        if len(table)!=32*count: raise ValueError('short index')
        offset=16+32*count; previous=''
        for i,r in enumerate(shaders):
            key,pos,n=struct.unpack_from('<16sQQ',table,i*32)
            name=key.decode('ascii')
            if not re.fullmatch('[0-9a-f]{16}',name) or name<=previous or name!=Path(r['path']).stem or pos!=offset or n!=r['bytes'] or n<32 or n>16*1024*1024 or n>total-offset:
                raise ValueError('archive index mismatch')
            sha=hashlib.sha256(); left=n
            while left:
                b=f.read(min(left,1024*1024))
                if not b: raise ValueError('truncated archive')
                sha.update(b); h.update(b); left-=len(b)
            if sha.hexdigest()!=r['sha256']: raise ValueError('archive payload mismatch')
            offset+=n; previous=name
        if offset!=total or f.read(1): raise ValueError('archive extent mismatch')
    sha=h.hexdigest()
    result=[r for r in rows if not r['path'].startswith('shaders/')]
    result.append({'path':'shaders.hnla','source':str(archive),'bytes':total,'sha256':sha})
    result.sort(key=lambda r:r['path'].encode('utf-16-be'))
    newraw=''.join(f"{r['sha256']}\t{r['bytes']}\t{r['path']}\n" for r in result).encode()
    data=dict(old); data.update(index_sha256=hashlib.sha256(newraw).hexdigest(),file_count=len(result),payload_bytes=sum(r['bytes'] for r in result),files=result,required={**old['required'],'shaders.hnla':sha})
    out.mkdir(parents=True,exist_ok=False)
    (out/'files.tsv').write_bytes(newraw); (out/'index.json').write_text(json.dumps(data,indent=2))
    print(json.dumps({'files':len(result),'archive_entries_verified':count,'archive_sha256':sha,'index_sha256':data['index_sha256'],'status':'LOCAL_NOT_DEVICE_ACCEPTED'}))

if __name__=='__main__': main()
