from pathlib import Path
import struct,json,hashlib,sys
P=Path(__file__).resolve().parent;N=Path(sys.argv[1]);frame=int(sys.argv[2]);Q=lambda b,o:struct.unpack_from('<Q',b,o)[0];U=lambda b,o:struct.unpack_from('<I',b,o)[0]
sh={r['module']:r['sha256']for r in json.load(open(P/'shader-manifest.json'))};pipes={r['pipeline']:r for r in json.load(open(P/'pipeline-manifest.json'))};wanted={}
for e in(1,2):
 wanted[e]={1:hashlib.sha256((N/f'f{frame}-e{e}-full-g{e}-shader-s0.bin').read_bytes()).hexdigest(),16:hashlib.sha256((N/f'f{frame}-e{e}-full-g{e}-shader-s4.bin').read_bytes()).hexdigest()}
match={p:e for p,r in pipes.items() for e,ws in wanted.items()if {s['stage']:sh[s['module']]for s in r['stages']}==ws};bound={};actual=[];state=True;current=None
for l in(P/'compact-events.jsonl').open():
 j=json.loads(l);k=j.get('kind');b=bytes.fromhex(j.get('rawhex',''));a=j.get('api')
 if k==2:
  current=Q(b,4)
  if U(b,0)==2:state=False
 elif k==1:current=Q(b,4)+1
 elif a==69725:bound[Q(b,0)]=Q(b,12)
 elif a in(69738,69739,69741) and not state and bound.get(Q(b,0))in match:
  actual.append({'index':j['index'],'event':match[bound[Q(b,0)]],'frame':current,'pipeline':bound[Q(b,0)],'cb':Q(b,0),'rawhex':b.hex(),'api':a})
selected=[r for r in actual if r['frame']==frame];assert len(selected)==2 and {r['event']for r in selected}=={1,2},(frame,actual)
(P/'discovered-targets.json').write_text(json.dumps({'native_frame':frame,'matching_pipeline_ids':match,'all_actual_matching_draws':actual,'selected':selected},indent=2));print(json.dumps(selected,indent=2))
