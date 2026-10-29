#!/usr/bin/env python3
"""Compile real current-source preflight functions against a dependency fixture."""
from pathlib import Path
import subprocess,json,hashlib,resource
root=Path(__file__).resolve().parents[2]
out=root/'.local/preflight-dependency';out.mkdir(exist_ok=True)
src=root/'src/graphics/nvn_vulkan/engine/xbg_game.cpp';data=src.read_text()
def function(signature):
 start=data.index(signature);a=data.index('{',start);depth=1;b=a+1
 while depth:
  depth+=(data[b]=='{')-(data[b]=='}');b+=1
 return data[start:b]+'\n'
text='\n'.join(function(s) for s in ['void resolve_pending(){','void preflight_cpu_reads(){','void cpu_access(uint64_t address,uint64_t bytes,unsigned why){'])
inc=out/'preflight_functions.inc';inc.write_text(text)
flags=['clang++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(out)]
fixture=root/'tests/native/preflight_dependency_contract.cpp';binary=out/'contract'
with (out/'build.log').open('w') as f:subprocess.run(flags+[str(fixture),'-o',str(binary)],stdout=f,stderr=f,check=True)
with (out/'run.log').open('w') as f:subprocess.run([str(binary)],stdout=f,stderr=f,check=True)
# Counterfactual: skipping the real wait must violate writeback visibility assertions.
inc.write_text(text.replace('gpu_sync(pending_writebacks.empty()?perf.sync_query:perf.sync_writeback);','/* deliberately omitted wait */'))
# Unused mock gpu_sync is expected only in this deliberately broken build.
with (out/'negative-build.log').open('w') as f:subprocess.run(flags+['-Wno-unused-function',str(fixture),'-o',str(out/'broken')],stdout=f,stderr=f,check=True)
def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with (out/'negative.log').open('w') as f:p=subprocess.run([str(out/'broken')],stdout=f,stderr=f,preexec_fn=no_core)
inc.write_text(text)
assert p.returncode!=0,'counterfactual falsely passed'
manifest={'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'baseline_exit':0,'negative_exit':p.returncode,'scope':'Mock completion ordering, not GPU or guest execution','compiler':subprocess.check_output(['clang++','--version'],text=True).splitlines()[0]}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2));print(json.dumps(manifest))
