from pathlib import Path
import json,re,hashlib,numpy as np
P=Path(__file__).resolve().parent;N=P/'dark-native-f8691';G=P/'dark-replay-gpu';frame=8691
rows=[(s.split(' ',1)[0],dict(re.findall(r'(\w+)=([^\s]+)',s)))for s in(N/'capture.txt').read_text().splitlines()]
api_matrices={r['draw']:r for r in json.load(open(P/'dark-api/target-resource-matrices.json'))}
items=[];missing=[];stage_id={'vertex':0,'fragment':4,'geometry':3,'tessellationControl':1,'tessellationEvaluation':2}
docs={j['drawIndex']:j for f in G.glob('*_dr.json')for j in json.load(open(f))if 'drawIndex'in j}
copy={(r['event'],r['label']):r for k,r in rows if k in('FULL_BUFFER_COPY','FULL_IMAGE_COPY')}
def nfile(r):return N/('f'+str(frame)+'-e'+r['event']+'-'+r['label']+'.bin')
def compare(label,n,rp,nbytes=None,depth=False,metadata=None,replay_offset=0):
 a=n.read_bytes();b=rp.read_bytes()[replay_offset:];nbytes=len(a)if nbytes is None else nbytes
 assert len(a)>=nbytes and len(b)>=nbytes,(label,len(a),len(b),nbytes)
 aa=a[:nbytes];bb=b[:nbytes]
 if depth:
  x=np.frombuffer(aa,dtype='<u4')&0xffffff;y=np.frombuffer(bb,dtype='<u4')&0xffffff;changed=int(np.count_nonzero(x!=y));unit='depth24-pixels'
 else:changed=int(np.count_nonzero(np.frombuffer(aa,dtype='u1')!=np.frombuffer(bb,dtype='u1')));unit='bytes'
 record=dict(label=label,native=n.name,replay=rp.name,compared_bytes=nbytes,native_bytes=len(a),replay_bytes=len(b),changed=changed,unit=unit,native_prefix_sha256=hashlib.sha256(aa).hexdigest(),replay_prefix_sha256=hashlib.sha256(bb).hexdigest())
 if metadata:record.update(metadata)
 items.append(record)
for event,draw in [('1',1046335),('2',1047825)]:
 doc=docs[draw];desc={(stage_id[s],r['binding'],r.get('arrayIndex',0)):r for s,ls in doc['descriptors'].items()for r in ls}
 for k,r in rows:
  if r.get('event')!=event:continue
  if k=='FULL_BUFFER_COPY'and r['phase']=='input':
   if r['role'].startswith('vertex'):
    binding=int(r['role'][6:]);a=next(x for x in doc['vertexBuffers']if x['vertexBufferBinding']==binding);f=G/Path(a['file']).name
    compare('e'+event+'-'+r['role'],nfile(r),f,f.stat().st_size,metadata={'scope':'exported reachable vertex prefix; native retains full bound range'})
   elif r['role']=='index':compare('e'+event+'-index',nfile(r),G/Path(doc['indexBuffer']['file']).name)
   elif re.fullmatch(r's\d+b\d+e\d+',r['role']):
    s,b,e=map(int,re.fullmatch(r's(\d+)b(\d+)e(\d+)',r['role']).groups());a=desc.get((s,b,e))
    if a is None:
     candidates=[x for x in api_matrices[draw]['rows']if x['binding']==b and x['element']==e and x['type']in(4,5)];assert len(candidates)==1,(r,candidates);v=candidates[0]
     files=list(G.glob('*_buffer_'+str(v['buffer'])+'_qs_'+str(doc['queueSubmitIndex'])+'_bcb_'+str(doc['beginCommandBufferIndex'])+'_rp_0.bin'));assert len(files)==1,(r,files)
     compare('e'+event+'-'+r['role'],nfile(r),files[0],replay_offset=v['offset'],metadata={'source':'GPU whole-buffer export sliced by actual same-draw view; no pointer-only dedup','bufferId':v['buffer'],'effectiveOffset':v['offset'],'effectiveRange':v['range'],'format':v['format']});continue
    assert isinstance(a['descriptor'],dict),a
    compare('e'+event+'-'+r['role'],nfile(r),G/Path(a['descriptor']['file']).name)
  elif k in('FULL_IMAGE_COPY','FULL_IMAGE_REF')and r['phase']=='input':
   m=re.fullmatch(r's(\d+)b(\d+)e(\d+)',r['role']);assert m,r;s,b,e=map(int,m.groups());a=desc.get((s,b,e))
   if a is None:missing.append(dict(event=event,role=r['role'],reason='image descriptor omitted'));continue
   source=copy[(event,r['label'])];aspect={1:'COLOR',2:'DEPTH',4:'STENCIL'}[int(r['aspect'])]
   matches=[x for x in a['descriptor']if x['mipLevel']==int(r['mip'])and x['arrayLayer']==int(r['layer'])and x['aspect']==aspect];assert len(matches)==1,(r,matches)
   f=G/Path(matches[0]['file']).name;compare('e'+event+'-'+r['role']+'-m'+r['mip']+'-l'+r['layer']+'-a'+r['aspect'],nfile(source),f,depth=aspect=='DEPTH')
  elif k in('FULL_IMAGE_COPY','FULL_IMAGE_REF')and r['phase']in('before','after'):
   aspect={1:'COLOR',2:'DEPTH',4:'STENCIL'}[int(r['aspect'])]
   target=doc['colorAttachments'][int(r['role'][5:])]if r['role'].startswith('color')else next(x for x in doc['depthAttachments']if x['aspect']==aspect)
   source=copy[(event,r['label'])];f=G/Path(target[r['phase']+'File']).name;compare('e'+event+'-'+r['phase']+'-'+r['role']+'-a'+r['aspect'],nfile(source),f,depth=aspect=='DEPTH',metadata={'dimensions':target['dimensions'],'aspect':aspect,'format':target['format']})
report={'scope':'Original nativeGPU vs unmodified same-frame replay; tight logical prefixes; D24 unusedX bits masked; vertex unused bound tail reported separately','items':items,'missing':missing,'CAPTURE_COMPLETE':'NO','replay_return_mismatch':'VK_NOT_READY vs capturedSUCCESS remains; actual data comparison is separate'}
(P/'dark-native-vs-replay.json').write_text(json.dumps(report,indent=2))
print(json.dumps({'compared':len(items),'exact':sum(r['changed']==0 for r in items),'different':[r for r in items if r['changed']],'missing':missing},indent=2))
