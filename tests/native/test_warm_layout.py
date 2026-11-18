from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
f=s[s.index('VkPipelineLayout make_layout('):s.index('VkRenderPass make_pass(')]
body=f[f.rindex('    std::lock_guard<std::mutex> l(mu);'):f.rindex('}')]
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'warm-layout-publish.inc').write_text(body)
 subprocess.run(['clang++','-std=c++17','-pthread','-fsanitize=address,undefined','-I',d,str(Path(__file__).with_suffix('.cpp')),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS actual warm layout publication: simultaneous duplicate and already-used winner, destroy losers only')
