"""Compile and execute the actual production retry function with a fake driver."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
a=s.index('static VkResult create_pipeline_cache_with_retry(')
b=s.index('extern "C" int xbg_game_initialize(',a)
code=r'''
#include <cassert>
#include <cstddef>
using VkDevice=int; using VkPipelineCache=int; using VkResult=int;
constexpr int VK_NULL_HANDLE=0,VK_SUCCESS=0,VK_ERROR_UNKNOWN=-13;
struct VkPipelineCacheCreateInfo { size_t initialDataSize; const void *pInitialData; };
int calls,first,second; bool expected_data;
void xbg_logf(const char*,...) {}
int vkCreatePipelineCache(int,const VkPipelineCacheCreateInfo* i,void*,int* out){
 assert((calls==0 && expected_data) ? i->initialDataSize==8 && i->pInitialData : !i->initialDataSize && !i->pInitialData);
 int r=calls++?second:first; *out=r?999:42; return r;
}
'''+s[a:b]+r'''
void test(int f,int n,bool data,int count,int result){
 calls=0;first=f;second=n;expected_data=data;int out=77;
 VkPipelineCacheCreateInfo info{data?8u:0u,data?&out:nullptr};
 assert(create_pipeline_cache_with_retry(1,info,&out)==result);
 assert(calls==count);assert(out==(result==0?42:0));
}
int main(){
 test(0,0,true,1,0);test(0,0,false,1,0);
 test(-13,0,true,2,0);test(-13,-13,true,2,-13);
 test(-13,-1,true,2,-1);test(-13,0,false,1,-13);
 for(int e:{-1,-2,-4,-3,-1000011001}) test(e,0,true,1,e);
}
'''
code=code.replace('#include <cassert>','#include <cassert>\n#include <initializer_list>')
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: actual production cache retry 11 cases with ASan/UBSan')

# Execute the real disk loader, including corrupted header rejection.
a=s.index('std::vector<uint8_t> load_pipeline_cache(){')
b=s.index('// The cache is written by its own thread:',a)
loader=r'''
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cassert>
struct { uint32_t vendorID=1,deviceID=2; unsigned char pipelineCacheUUID[16]={}; } properties;
const char *path; std::string pipeline_cache_path(){return path;}
void xbg_logf(const char*,...) {}
'''+s[a:b]+r'''
void fixture(uint32_t size,uint32_t version,uint32_t vendor,uint32_t device,int uuid,size_t count,bool valid){
 uint32_t raw[16]={size,version,vendor,device};
 reinterpret_cast<unsigned char*>(raw)[16]=uuid;
 FILE*f=fopen(path,"wb");assert(f);assert(fwrite(raw,1,count,f)==count);fclose(f);
 assert(!load_pipeline_cache().empty()==valid);
}
int main(int argc,char**argv){
 assert(argc==2);path=argv[1];assert(load_pipeline_cache().empty());
 fixture(32,1,1,2,0,64,true);
 for(unsigned n:{0u,16u,31u,33u,64u,0xffffffffu}) fixture(n,1,1,2,0,64,false);
 fixture(32,2,1,2,0,64,false);fixture(32,1,3,2,0,64,false);
 fixture(32,1,1,3,0,64,false);fixture(32,1,1,2,1,64,false);
 fixture(32,1,1,2,0,16,false);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'loader.cpp').write_text(loader)
 subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'loader.cpp'),'-o',str(p/'loader')],check=True)
 subprocess.run([str(p/'loader'),str(p/'cache')],check=True)
print('PASS: actual disk loader rejects invalid V1 header sizes and identity')
