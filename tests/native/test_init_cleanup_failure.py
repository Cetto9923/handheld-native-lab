"""Actual adapter initialization branch, with injected engine outcomes (no Vulkan)."""
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'src/graphics/nvn_vulkan/nvn_vk_adapter.c').read_text()
start=s.index('int xbg_vk_initialize('); end=s.index('\nvoid mtl_init(',start)
body=s[start:end]
pre=r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
typedef struct { unsigned size; struct { int present; } game; int sampler_mirror_clamp_to_edge; } xbg_vk_config;
static int g_ready, init_result, sampler_result, cleanup_result, init_calls, sampler_calls, cleanup_calls;
static xbg_vk_config g_config;
static int xbg_game_initialize(const void *c,char *error,size_t bytes){(void)c;(void)error;(void)bytes;++init_calls;return init_result;}
static int xbg_game_set_sampler_mirror_clamp_to_edge(int enabled){(void)enabled;++sampler_calls;return sampler_result;}
static int xbg_game_shutdown(void){++cleanup_calls;return cleanup_result;}
static void vk_die(const char *fmt,...){assert(init_calls==1&&sampler_calls==1&&cleanup_calls==1&&!g_ready);va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);fflush(NULL);_Exit(70);}
'''
main=r'''
int main(int argc,char **argv){assert(argc==2);int n=atoi(argv[1]);xbg_vk_config c={sizeof(c),{1},1};char error[128]={0};
 if(n==1)init_result=-7;
 if(n==2||n==3)sampler_result=-9;
 if(n==3)cleanup_result=-4;
 if(n==4)c.game.present=0;
 if(n==5)g_ready=1;
 if(n==7)c.size=0;
 int r=xbg_vk_initialize(n==6?NULL:&c,error,sizeof(error));
 if(n==0){assert(r==0&&g_ready&&init_calls==1&&sampler_calls==1&&cleanup_calls==0&&g_config.game.present==1);}
 else if(n==1){assert(r==-7&&!g_ready&&init_calls==1&&!sampler_calls&&!cleanup_calls);}
 else if(n==2){assert(r==-9&&!g_ready&&init_calls==1&&sampler_calls==1&&cleanup_calls==1);}
 else if(n==3){assert(!"dual failure returned to guest");}
 else {assert(r==-1&&g_ready==(n==5)&&!init_calls&&!sampler_calls&&!cleanup_calls);}
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'case.c').write_text(pre+body+main)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'case.c'),'-o',str(p/'case')],check=True)
 for n in range(8):
  r=subprocess.run([str(p/'case'),str(n)],capture_output=True,text=True)
  assert r.returncode==(70 if n==3 else 0),(n,r.returncode,r.stderr)
  if n==3:assert 'init=-9 cleanup=-4' in r.stderr
 # Regression-negative: old ignored-cleanup implementation must fail this test.
 old=body.replace('if (cleanup) vk_die("Vulkan initialization failed init=%d cleanup=%d; retained device requires process exit", r, cleanup);','(void)cleanup;')
 (p/'old.c').write_text(pre+old+main)
 subprocess.run(['clang','-std=c11','-Wno-unused-function',str(p/'old.c'),'-o',str(p/'old')],check=True)
 negative=subprocess.run([str(p/'old'),'3'],capture_output=True,text=True)
 assert negative.returncode!=70 and 'dual failure returned to guest' in negative.stderr
fatal=(ROOT/'src/graphics/nvn_device/nvn_engine.c').read_text().split('static void engine_fatal(',1)[1].split('\nvoid nvn_engine_set_enabled',1)[0]
assert 'engine_write_fatal_file(text)' in fatal and '_exit(70);' in fatal
assert 'xbg_fatalf("%s", buffer);' in s
print('PASS actual init branch: success/early failure/clean rollback unchanged; dual failure terminal70 with both errors; old-branch negative fails; ASan/UBSan. Real device path NOT_INJECTED.')
