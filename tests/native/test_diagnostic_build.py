from pathlib import Path
import subprocess,tempfile,sys,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
script=(root/'scripts/android/build_game_window_apk.sh').read_text()
code=script.split("<<'PY_MANIFEST'\n",1)[1].split('\nPY_MANIFEST',1)[0]
header=root/'src/graphics/nvn_vulkan/engine/xbg_api_breadcrumb.h'
engine=(root/'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
assert 'gfx-api-breadcrumb' not in engine
assert 'const bool on=FFRS_API_BREADCRUMB_DIAGNOSTIC != 0;' in engine
assert '-DFFRS_API_BREADCRUMB_DIAGNOSTIC="${FFRS_API_BREADCRUMB_DIAGNOSTIC}"' in script
cmake=(root/'src/android/native/CMakeLists.txt').read_text()
assert 'target_compile_definitions(ffrs_nvn_vulkan_engine PRIVATE FFRS_API_BREADCRUMB_DIAGNOSTIC=${FFRS_API_BREADCRUMB_DIAGNOSTIC})' in cmake
with tempfile.TemporaryDirectory() as td:
 t=Path(td); src=t/'manifest.xml'; src.write_text('<manifest xmlns:android="http://schemas.android.com/apk/res/android" android:versionName="1.0.72" android:versionCode="72"/>')
 original=src.read_bytes()
 for mode,expected in [('0','1.0.72'),('1','1.0.72-mali-diag')]:
  dst=t/f'manifest{mode}.xml'
  subprocess.run([sys.executable,'-c',code,str(src),str(dst),mode],check=True)
  assert ET.parse(dst).getroot().get('{http://schemas.android.com/apk/res/android}versionName')==expected
 assert src.read_bytes()==original
 assert subprocess.run([sys.executable,'-c',code,str(t/'manifest1.xml'),str(t/'bad.xml'),'0'],capture_output=True).returncode!=0
 cpp=t/'test.cpp';cpp.write_text('#include "'+str(header)+'"\nint main(int argc,char **argv){XbgApiBreadcrumb b;b.configure(FFRS_API_BREADCRUMB_DIAGNOSTIC!=0,argv[1]);auto n=b.begin(XbgApiBreadcrumb::Target,XbgApiBreadcrumb::Dispatch,1);b.end(n,XbgApiBreadcrumb::Dispatch,1,0);}\n')
 for mode in [None,0,1]:
  exe=t/('test'+str(mode));cmd=['clang++','-std=c++17','-pthread','-fsanitize=address,undefined',str(cpp),'-o',str(exe)]
  if mode is not None:cmd+=['-DFFRS_API_BREADCRUMB_DIAGNOSTIC='+str(mode)]
  subprocess.run(cmd,check=True);log=t/('log'+str(mode));subprocess.run([str(exe),str(log)],check=True)
  assert log.exists()==(mode==1)
  if mode==1: assert len(log.read_text().splitlines())==3
  else:
   log.write_text('old-session\n');subprocess.run([str(exe),str(log)],check=True);assert log.read_text()=='old-session\n'
 assert subprocess.run(['clang++','-std=c++17','-DFFRS_API_BREADCRUMB_DIAGNOSTIC=2','-fsyntax-only',str(cpp)],capture_output=True).returncode!=0
subprocess.run(['bash','-n',str(root/'scripts/android/build_game_window_apk.sh')],check=True)
print('PASS: build modes/default off/stale log unchanged/manifest suffix/reject mislabel/invalid macro/ASan UBSan')
