"""Run the production depth omission predicate and cache eligibility expression."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DepthAttachment(unittest.TestCase):
    def test_ordinary_draw_and_cache_state_changes(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        begin = source.index('const bool omit_depth=')
        predicate = source[begin:source.index(';', begin) + 1]
        begin = source.index('if(plain_draw && last_pass.valid')
        end = source.index('){', begin)
        cache_expression = source[begin + 3:end]
        fixture = r'''
#include <cassert>
#include <cstring>
struct { bool active=false, probe=false; } capture;
struct {
    struct { int n=1; const void *color[8]={}; const void *depth=nullptr; } rt;
    struct { bool dt=false, st=false; } ds;
    struct { bool enable=false; } bounds;
    struct { bool offset_en=false; } poly;
} state;
const void *provenance_file=nullptr;
static bool force_old=false;
bool ab_off(const char *name) { assert(!strcmp(name,"depthomit")); return force_old; }
struct { bool draw=true; int command=1; } c;
struct CachedPass { int pass=1, framebuffer=2; bool depth_omitted=false; };
struct { decltype(state.rt) rt; CachedPass pass; bool valid=true; } last_pass;
struct { int cmd=1, pass=1, framebuffer=2; } open_pass;
bool eligible(int clear_color=-1, bool clear_depth=false, bool soft=false, int fold_clear_slot=-1) {
''' + predicate + r'''
    return omit_depth;
}
bool cache_hit(bool omit_depth, bool plain_draw=true) {
    return ''' + cache_expression + r''';
}
int main() {
    int color=1, depth=2;
    state.rt.color[0]=&color;
    state.rt.depth=&depth;
    assert(eligible());
    assert(!eligible(0) && !eligible(-1,true) && !eligible(-1,false,true));
    assert(!eligible(-1,false,false,0));
    state.ds.dt=true; assert(!eligible()); state.ds.dt=false;
    state.ds.st=true; assert(!eligible()); state.ds.st=false;
    state.bounds.enable=true; assert(!eligible()); state.bounds.enable=false;
    state.poly.offset_en=true; assert(!eligible()); state.poly.offset_en=false;
    state.rt.n=0; assert(!eligible());
    state.rt.n=2; assert(!eligible()); state.rt.n=1;
    state.rt.color[0]=nullptr; assert(!eligible()); state.rt.color[0]=&color;
    state.rt.depth=nullptr; assert(!eligible()); state.rt.depth=&depth;
    c.draw=false; assert(!eligible()); c.draw=true;
    capture.active=true; assert(!eligible()); capture.active=false;
    capture.probe=true; assert(!eligible()); capture.probe=false;
    provenance_file=&depth; assert(!eligible()); provenance_file=nullptr;
    force_old=true; assert(!eligible()); force_old=false;
    last_pass.rt=state.rt;
    last_pass.pass.depth_omitted=true;
    assert(cache_hit(true) && !cache_hit(false));
    // Enabling any test restores the original attachment and must miss the
    // color-only pass even though the guest render-target pointers are equal.
    state.ds.dt=true; assert(!cache_hit(eligible())); state.ds.dt=false;
    state.ds.st=true; assert(!cache_hit(eligible())); state.ds.st=false;
    state.bounds.enable=true; assert(!cache_hit(eligible())); state.bounds.enable=false;
    last_pass.pass.depth_omitted=false;
    assert(!cache_hit(true) && cache_hit(false));
    assert(!cache_hit(false,false));
    open_pass.pass=3; assert(!cache_hit(false)); open_pass.pass=1;
    state.rt.color[0]=&depth; assert(!cache_hit(false));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.cpp'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)
        planning = source.index('int result=xbg_framebuffer_plan_layered(')
        omission = source.index('if(omit_depth)targets[8]=nullptr;')
        enumeration = source.index('for(unsigned slot=0;slot<9;slot++)if(targets[slot])', omission)
        self.assertLess(planning, omission)
        self.assertLess(source.index('p.width=plan.width;p.height=plan.height;'), omission)
        self.assertLess(omission, enumeration)
        self.assertIn('p.depth_attached=targets[8]!=nullptr;', source)
        self.assertEqual(source.count('open_pass.depth_attached=p.depth_attached;'), 2)
        self.assertNotIn('open_pass.depth_attached=state.rt.depth!=nullptr;', source)


if __name__ == '__main__':
    unittest.main()
