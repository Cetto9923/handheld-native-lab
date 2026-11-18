#!/usr/bin/env python3
"""Standalone production-header contract; no Vulkan or device dependency."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
SOURCE=r'''
#include "xbg_surface_lifecycle.h"
#include <cassert>
#include <condition_variable>
#include <thread>
#include <stdexcept>
int main(){
 XbgSurfaceLifecycle l;using T=XbgSurfaceLifecycle::Token;T a{},b{};int called=0;
 assert(!l.admit(0,[&](T){++called;}));
 assert(l.reopen(1,[&](T t){assert(t.owner==1);assert(!l.admit(1,[](T){}));return true;},&a));
 assert(l.admit(0,[&](T t){assert(t==a);assert(l.admit(1,[](T){}));++called;}));
 assert(!l.admit(2,[](T){}));assert(!l.reopen(2,[](T){return true;}));
 assert(!l.close(T{1,a.generation+1},[](T){assert(false);return true;}));
 assert(!l.close(a,[](T){return false;}));assert(!l.state().open&&!l.state().retired);
 assert(!l.admit(1,[](T){}));assert(!l.reopen(2,[](T){return true;}));
 assert(l.close(a,[](T){return true;}));assert(l.close(a,[](T){assert(false);return true;}));
 assert(l.reopen(2,[](T){return true;},&b));assert(b.generation>a.generation);
 assert(!l.close(a,[](T){assert(false);return true;}));
 assert(l.close(b,[](T){return true;}));
 assert(!l.reopen(3,[](T){return false;}));T failed=l.state().token;
 assert(!l.state().retired&&!l.state().open);assert(l.close(failed,[](T){return true;}));
 assert(l.reopen(4,[](T){return true;},&a));
 try{l.close(a,[](T)->bool{throw std::runtime_error("fail");});assert(false);}catch(const std::runtime_error&){}
 assert(!l.state().open&&!l.state().retired);assert(l.close(a,[](T){return true;}));
 // Deterministic enqueue -> close -> drain. Worker intentionally NEVER takes admission.
 assert(l.reopen(5,[](T){return true;},&a));
 std::mutex m;std::condition_variable cv;bool queued=false,go=false,done=false,destroyed=false,closing=false;
 std::thread worker([&]{std::unique_lock<std::mutex>k(m);cv.wait(k,[&]{return go;});assert(queued&&!destroyed);done=true;cv.notify_all();});
 assert(l.admit(5,[&](T t){assert(t==a);std::lock_guard<std::mutex>k(m);queued=true;}));
 std::thread closer([&]{assert(l.close(a,[&](T){std::unique_lock<std::mutex>k(m);closing=true;cv.notify_all();cv.wait(k,[&]{return done;});destroyed=true;return true;}));});
 {std::unique_lock<std::mutex>k(m);cv.wait(k,[&]{return closing;});assert(!destroyed);go=true;}cv.notify_all();
 worker.join();closer.join();assert(destroyed);assert(!l.admit(5,[](T){assert(false);}));
 assert(l.reopen(5,[](T){return true;},&b));assert(!(a==b));assert(!l.close(a,[](T){assert(false);return true;}));
 assert(l.close(b,[](T){return true;}));
 uint64_t epoch=l.state().token.generation;
 try{l.reopen(6,[](T)->bool{throw std::runtime_error("partial setup");});assert(false);}catch(const std::runtime_error&){}
 auto partial=l.state();assert(partial.token.owner==6&&partial.token.generation==epoch+1&&!partial.open&&!partial.retired);
 assert(!l.reopen(7,[](T){return true;}));assert(l.close(partial.token,[](T){return true;}));
 assert(l.reopen(7,[](T){return true;},&b));
 try{l.admit(7,[](T){throw std::runtime_error("producer");});assert(false);}catch(const std::runtime_error&){}
 assert(l.admit(7,[](T){}));assert(l.close(b,[](T){return true;}));
 assert(called==1);
}
'''
with tempfile.TemporaryDirectory(prefix='surface-lifecycle-') as d:
 p=Path(d);(p/'test.cpp').write_text(SOURCE)
 subprocess.run(['clang++','-std=c++17','-pthread','-fsanitize=address,undefined','-I',str(ROOT/'src/graphics/nvn_vulkan/engine'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=20)
print('surface lifecycle contracts PASS (ASan/UBSan; renderer integration not tested)')
