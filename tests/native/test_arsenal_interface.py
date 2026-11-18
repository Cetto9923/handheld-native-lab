"""Exact-module runtime transform contract; proprietary inputs stay outside Git."""
import os,pathlib,re,subprocess,tempfile,struct,unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class ArsenalInterface(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.t=tempfile.TemporaryDirectory();cls.dir=pathlib.Path(cls.t.name);src=cls.dir/'probe.cpp'
  src.write_text(r'''
#include "xbg_arsenal_interface.h"
#include <fstream>
int main(int argc,char**argv){if(argc!=4)return 2;std::ifstream f(argv[1],std::ios::binary|std::ios::ate);if(!f)return 3;size_t n=f.tellg();if(n%4)return 4;f.seekg(0);std::vector<uint32_t>w(n/4);f.read((char*)w.data(),n);bool ok=xbg_arsenal_complete_output(w,argv[3]);std::ofstream o(argv[2],std::ios::binary);o.write((char*)w.data(),w.size()*4);return ok?0:7;}
''')
  cls.exe=cls.dir/'probe';subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(ROOT/'src/graphics/nvn_vulkan/engine'),str(src),'-o',str(cls.exe)],check=True,capture_output=True)
 @classmethod
 def tearDownClass(cls):cls.t.cleanup()
 def apply(self,b,key):
  i=self.dir/'in.spv';o=self.dir/'out.spv';i.write_bytes(b);p=subprocess.run([str(self.exe),str(i),str(o),key],capture_output=True);return p.returncode,o.read_bytes()
 def test_unknown_and_empty(self):
  for b in [b'',struct.pack('<5I',0x07230203,0x10300,0,3,0)]:
   self.assertEqual(self.apply(b,'unknown'),(7,b))
 def test_real_modules_roundtrip(self):
  path=os.environ.get('FFRS_ARSENAL_SPV_DIR')
  if not path:self.skipTest('explicit real-module fixture directory not supplied')
  for key,location in [('17c6ba1e659fec3a',7),('5a97a7e1a99cce45',7),('3bce5482c09be3fb',4)]:
   self.check_real_module(path, key, location)

 def check_real_module(self, path, key, location):
  b=(pathlib.Path(path)/(key+'.spv')).read_bytes();code,patched=self.apply(b,key);self.assertEqual(code,0,key)
  a=list(struct.unpack('<'+'I'*(len(b)//4),b));w=list(struct.unpack('<'+'I'*(len(patched)//4),patched));bound=a[3];self.assertEqual(len(w),len(a)+27)
  self.assertEqual(w[3],bound+4);recovered=w[:5];recovered[3]=bound;i=5;stores=0;decls=0
  while i<len(w):
   n=w[i]>>16;op=w[i]&65535;ins=w[i:i+n]
   if op==71 and ins[1]==bound+3:self.assertEqual(ins[2:],[30,location]);decls+=1
   elif op in [43,44,59] and ins[2] in range(bound,bound+4):pass
   elif op==62 and ins[1]==bound+3:self.assertEqual(ins[2],bound+2);stores+=1
   elif op==15:
    self.assertEqual(ins[-1],bound+3);ins=ins[:-1];ins[0]=((n-1)<<16)|15;recovered.extend(ins)
   else:recovered.extend(ins)
   i+=n
  self.assertEqual((decls,stores),(1,1));self.assertEqual(recovered,a)
  out=self.dir/(key+'-patched.spv');out.write_bytes(patched)
  subprocess.run(['spirv-val','--target-env','vulkan1.3',str(out)],check=True,capture_output=True)
  self.assertEqual(self.apply(patched,key),(7,patched))
  altered=bytearray(b);altered[-1]^=1;self.assertEqual(self.apply(bytes(altered),key),(7,bytes(altered)))
  return patched

 def test_airship_integer_variant(self):
  path = os.environ.get('FFRS_AIRSHIP_SPV_DIR')
  if not path:
   self.skipTest('explicit airship shader fixture directory not supplied')
  fixtures = pathlib.Path(path)
  key = '0d79843412724995'
  patched = self.dir / 'airship-vs-patched.spv'
  patched.write_bytes(self.check_real_module(fixtures, key, 7))
  # Use the production binding-18 rewrite from the failing integer variant.
  rewrite = self.dir / 'rewrite-integer-buffer'
  subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                  '-fsanitize=address,undefined', str(ROOT / 'tests/device/test_reflection_uint_fetch.cpp'),
                  '-o', str(rewrite)], check=True)

  def locations(module, direction):
   subprocess.run(['spirv-val', '--target-env', 'vulkan1.3', str(module)], check=True)
   assembly = subprocess.check_output(['spirv-dis', '--raw-id', str(module)], text=True)
   variables = set(re.findall(r'(%[0-9]+) = OpVariable %[0-9]+ ' + direction + r'\b', assembly))
   return {int(loc) for var, loc in re.findall(r'OpDecorate (%[0-9]+) Location ([0-9]+)', assembly)
           if var in variables}

  inputs = locations(fixtures / '5e2abb57bc78f89b.spv', 'Input')
  self.assertIn(7, inputs)
  for source, missing in [(fixtures / (key + '.spv'), {7}), (patched, set())]:
   variant = self.dir / 'ib1-18-98.spv'
   subprocess.run([str(rewrite), '--module', str(source), str(variant), '18'], check=True)
   self.assertEqual(inputs - locations(variant, 'Output'), missing)

if __name__=='__main__':unittest.main()
