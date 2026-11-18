"""Extract the production bounded failure recorder, without a Vulkan driver."""
import pathlib, subprocess, tempfile, unittest, struct
ROOT = pathlib.Path(__file__).resolve().parents[2]
class FailureRecord(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(); cls.base=pathlib.Path(cls.tmp.name)
  s=(ROOT/'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
  fn=s[s.index('void record_failure('):s.index('void record(const VkGraphicsPipelineCreateInfo &ci',s.index('void record_failure('))]
  stub=r'''
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <unordered_map>
#include <vector>
using VkResult=int;
struct VkPipelineShaderStageCreateInfo {uint64_t module;};
struct XbgPipelineKey {uint32_t words[4096]{};uint32_t count=4;bool valid=true;};
std::mutex mu;
std::unordered_map<uint64_t,uint64_t> ids{{1,10}};
std::unordered_map<uint64_t,std::string> id_keys{{10,"17c6ba1e659fec3a"}};
struct {std::atomic<uint64_t> presents{42};} stats;
std::string directory;
std::string list_path(){return directory+"/vk-pipeline-list.bin";}
void xbg_logf(const char*,...){}
'''
  main=r'''
int main(int argc,char **argv){
 if(argc!=3)return 2;directory=argv[1];std::string mode=argv[2];
 VkPipelineShaderStageCreateInfo stage{1};XbgPipelineKey key;key.words[0]=123;
 if(mode=="invalid")key.valid=false;
 if(mode=="oversized")key.count=4097;
 if(mode=="unknown")stage.module=2;
 record_failure(&stage,1,key,-13,12345678);
 if(mode=="normal")record_failure(&stage,1,key,-13,12345678);
 if(mode=="cap")for(uint32_t i=1;i<30;++i){key.words[0]=i;record_failure(&stage,1,key,-13,12345678);}
 return 0;
}
'''
  src=cls.base/'probe.cpp';src.write_text(stub+fn+main);cls.exe=cls.base/'probe'
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(cls.exe)],check=True,capture_output=True)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def run_case(self,mode,prepare=None):
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d)
   if prepare:prepare(p)
   subprocess.run([str(self.exe),d,mode],check=True,capture_output=True)
   f=p/'pipeline-failures.log';return f.read_bytes() if f.exists() else b''
 def test_roundtrip_and_duplicate(self):
  b=self.run_case('normal');self.assertEqual(b.count(b'pipeline_failure'),1)
  self.assertIn(b'frame=42 result=-13 elapsed_ns=12345678',b)
  raw=bytes.fromhex(b.decode().split('recipe_hex=')[1].strip());magic,count,size=struct.unpack_from('<III',raw)
  self.assertEqual((magic,count,size),(0x57504c58,1,16));self.assertEqual(raw[12:28],b'17c6ba1e659fec3a')
  self.assertEqual(struct.unpack_from('<5I',raw,28),(4,123,0,0,0))
 def test_eight_recipe_cap(self):self.assertEqual(self.run_case('cap').count(b'pipeline_failure'),8)
 def test_invalid(self):self.assertEqual(self.run_case('invalid'),b'')
 def test_oversized(self):self.assertEqual(self.run_case('oversized'),b'')
 def test_unknown(self):self.assertEqual(self.run_case('unknown'),b'')
 def test_file_cap(self):
  b=self.run_case('normal',lambda p:(p/'pipeline-failures.log').write_bytes(b'x'*(512*1024)));self.assertEqual(len(b),512*1024)
 def test_symlink(self):
  def prep(p):
   (p/'target').write_bytes(b'unchanged');(p/'pipeline-failures.log').symlink_to(p/'target')
  self.assertEqual(self.run_case('normal',prep),b'unchanged')
 def test_allowlist(self):
  s=(ROOT/'src/android/app/java/org/ffrs/diagnostic/DiagnosticReport.java').read_text();self.assertIn('"pipeline-failures.log"',s)
if __name__=='__main__':unittest.main()
