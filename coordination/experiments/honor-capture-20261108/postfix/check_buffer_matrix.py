from pathlib import Path
import json,re,sys
P=Path(__file__).resolve().parent;role=sys.argv[1];frame=int(sys.argv[2]);N=P/(role+'-native-f'+str(frame));A=P/(role+'-api');targets={r['event']:r for r in json.load(open(A/'discovered-targets.json'))['selected']};matrices={r['draw']:r for r in json.load(open(A/'target-resource-matrices.json'))};out=[]
for line in(N/'capture.txt').read_text().splitlines():
 if not line.startswith('FULL_BUFFER_BIND '):continue
 r=dict(re.findall(r'(\w+)=([^\s]+)',line));draw=targets[int(r['event'])]['index'];rows=[x for x in matrices[draw]['rows']if x['binding']==int(r['binding'])and x['element']==int(r['element'])];assert len(rows)==1,(r,rows);v=rows[0];off=v.get('effective_offset',v['offset']);assert off==int(r['offset'])and v['range']==int(r['range'])and v.get('format',0)==int(r['format']),(r,v);out.append({'event':int(r['event']),'draw':draw,'stage':int(r['stage']),'binding':int(r['binding']),'element':int(r['element']),'offset':off,'range':v['range'],'format':v.get('format',0),'APIbuffer':v['buffer'],'APIdescriptorUpdate':matrices[draw]['updated'],'APIviewCreated':v.get('created'),'nativeBuffer':r['buffer'],'nativeSlotGeneration':int(r['slotGeneration']),'matched':True})
assert out
(P/(role+'-buffer-binding-agreement.json')).write_text(json.dumps({'frame':frame,'items':out,'dedup':'None. Every stage/binding/element/offset/range is retained separately; handle identity is local to native or API namespaces.'},indent=2));print('matched buffer slices',len(out))
