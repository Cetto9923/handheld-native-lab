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
