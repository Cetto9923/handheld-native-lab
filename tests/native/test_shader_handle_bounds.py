"""C2 table sizes are entry counts; sampler words must not alias textures."""
import importlib.util
from pathlib import Path
p=Path(__file__).resolve().parents[2]/'scripts/graphics/build_ffrs_xgs.py'
spec=importlib.util.spec_from_file_location('ffrs_xgs',p)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
for texture in (0,1,31):
    handle=(1<<28)|((0x20+texture*8)//4)|(((0x568+8)//4)<<14)
    assert m.decode_handle(2,handle,False)==(texture,1,1,'combined_texture')
for texture in (0,1,127):
    handle=(1<<28)|((0x168+texture*8)//4)|(((0x568+127*8)//4)<<14)
    assert m.decode_handle(2,handle,False)==(texture,127,1,'separate_texture')
assert m.decode_handle(2,(0x568+8)//4,False) is None
assert m.decode_handle(2,0x120//4,True)==(0,None,0,'image')
assert m.decode_handle(2,0x160//4,True) is None
assert m.decode_handle(2,0x48,False)==(0,None,0,'image_texture')
print('shader handle boundary/isolation contract PASS')
