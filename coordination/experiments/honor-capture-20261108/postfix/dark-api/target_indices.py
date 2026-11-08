from pathlib import Path
import struct,json,hashlib
P=Path(__file__).resolve().parent
U=lambda b,o:struct.unpack_from('<I',b,o)[0]
Q=lambda b,o:struct.unpack_from('<Q',b,o)[0]
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
j=json.loads((P/'api-known-control.json').read_text())['vkQueueSubmit'];assert submit(bytes.fromhex(j['rawhex']))==[]
want=[r['index'] for r in json.load(open(P/'discovered-targets.json'))['selected']];beg={};rp={};rows=[];sub=[]
for l in (P/'compact-events.jsonl').open():
 j=json.loads(l);a=j.get('api');b=bytes.fromhex(j.get('rawhex',''));i=j['index']
 if a==69722:beg[Q(b,0)]=i
 if a==69765:rp[Q(b,0)]=i
 if i in want:rows.append({'draw':i,'cb':Q(b,0),'begin':beg[Q(b,0)],'rp':rp[Q(b,0)]})
 if a==69767:
  for r in rows:
   if r['cb']==Q(b,0) and 'end' not in r:r['end']=i
 if a==69650:sub.append((i,submit(b)))
for r in rows:
 r['submit']=next(i for i,cbs in sub if i>r['end'] and r['cb'] in cbs)
(P/'target-draw-identities.json').write_text(json.dumps(rows,indent=2))
(P/'target-draw-indices.txt').write_text(''.join(f"BeginCommandBuffer={r['begin']},Draw={r['draw']},BeginRenderPass={r['rp']},EndRenderPass={r['end']},QueueSubmit={r['submit']}\n" for r in rows))
print(json.dumps(rows,indent=2))
