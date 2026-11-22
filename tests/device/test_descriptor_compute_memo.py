"""Contract for the P0 descriptor memo that also covers compute dispatches.

Only draws were memoized before, so every dispatch rebuilt its whole descriptor set. The memo key
has to describe exactly the stages descriptors_build() binds - the five graphics stages for a draw,
the compute stage for a dispatch - and the two key shapes must never alias each other in the shared
memo map. The dispatch path keeps the guards the draw path has: a dispatch that needs the indirect
draw parameters, or that binds a per-dispatch texel view, stays out of the memo.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp'


class DescriptorComputeMemo(unittest.TestCase):
    def source(self):
        return SOURCE.read_text()

    def descriptors_body(self):
        source = self.source()
        start = source.index('void descriptors(Descriptors &out,Commands &commands,bool compute){')
        end = source.index('void descriptors_build(', start)
        return source[start:end]

    def test_draw_key_keeps_the_five_graphics_stages(self):
        body = self.descriptors_body()
        self.assertIn('else for(unsigned st=0;st<5;st++){key.push_back(uintptr_t(state.shader[st]));', body)

    def test_dispatch_key_carries_the_compute_stage_and_its_own_marker(self):
        body = self.descriptors_body()
        self.assertIn('if(compute){key.push_back(uintptr_t(state.shader[5]));key.push_back(kDescMemoComputeMarker);', body)

    def test_the_binding_walk_matches_the_stage_selection_of_the_build(self):
        self.assertIn('for(unsigned st=0;st<6 && ok;st++)if(state.shader[st] && (st==5)==compute){',
                      self.descriptors_body())
        source = self.source()
        build_start = source.index('void descriptors_build(')
        build_end = source.index('// Render passes are cached by their full definition;', build_start)
        build = source[build_start:build_end]
        self.assertIn('for(unsigned st=0;st<6;st++)if(state.shader[st] && (st==5)==compute)', build)

    def test_the_dynamic_uniform_mode_matches_the_build_for_a_dispatch(self):
        self.assertIn('const unsigned dmode=dyn_mode_for(compute,&no_ubo);', self.descriptors_body())

    def test_a_dispatch_stays_out_of_the_memo_when_it_needs_the_indirect_parameters(self):
        body = self.descriptors_body()
        self.assertIn('const bool compute_memo = compute && desc_memo_compute_enabled && state.shader[5] &&',
                      body)
        self.assertIn('!(out.indirect_input && !out.indirect_input->instances_only)', body)

    def test_dispatch_texel_views_are_not_memoized(self):
        self.assertIn('(b.type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER && (!texel_memo_enabled || compute))',
                      self.descriptors_body())

    def test_the_switch_is_registered_as_a_live_observation_switch(self):
        source = self.source()
        self.assertIn('{"desccompute",&desc_memo_compute_enabled}', source)
        self.assertIn('bool desc_memo_compute_enabled=true;', source)
        # The switch may only reach the descriptor path: it must not touch the game or the save.
        self.assertEqual(self.descriptors_body().count('desc_memo_compute_enabled'), 1)

    def test_the_dispatch_counters_are_reported(self):
        source = self.source()
        for token in ('desc_memo_compute_hits', 'desc_memo_compute_misses', 'desc_memo_compute_why[6]'):
            self.assertIn(token, source)

    def test_a_draw_key_and_a_dispatch_key_can_never_alias(self):
        # Behavioural check: the production prologue itself decides the two key shapes.
        source = self.source()
        line_draw = next(line for line in source.splitlines()
                         if 'else for(unsigned st=0;st<5;st++){key.push_back(uintptr_t(state.shader[st]));' in line)
        line_compute = next(line for line in source.splitlines()
                            if 'if(compute){key.push_back(uintptr_t(state.shader[5]));key.push_back(kDescMemoComputeMarker);' in line)
        marker = re.search(r'kDescMemoComputeMarker=(0x[0-9a-fA-F]+)ull', source).group(1)
        fixture = r"""
#include <cassert>
#include <cstdint>
#include <vector>
struct ShaderStub { uintptr_t pointer=0; };
struct StateStub { ShaderStub *shader[6]={}; } state;
std::vector<uint64_t> key; std::vector<uint8_t> ktag; bool tags=true;
constexpr uint64_t kDescMemoComputeMarker=MARKER;
void build_key(bool compute) {
    key.clear(); ktag.clear();
""" + line_compute.strip() + "\n" + line_draw.strip() + r"""
}
int main() {
    ShaderStub shaders[6];
    for(unsigned i=0;i<6;++i){ shaders[i].pointer=0x1000+i*8; state.shader[i]=&shaders[i]; }
    for(unsigned draw=0;draw<6;++draw){
        build_key(false);
        const std::vector<uint64_t> draw_key=key;
        assert(draw_key.size()==5);
        for(size_t i=0;i<draw_key.size();++i) assert(draw_key[i]!=kDescMemoComputeMarker);
        for(unsigned cs=0;cs<6;++cs){
            state.shader[5]=&shaders[cs];
            build_key(true);
            assert(key.size()==2);
            assert(key[1]==kDescMemoComputeMarker);
            assert(key!=draw_key);
        }
    }
    return 0;
}
"""
        fixture = fixture.replace('MARKER', marker)
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            unit = temporary / 'key.cpp'
            unit.write_text(fixture)
            binary = temporary / 'key'
            compile_result = subprocess.run(
                ['clang++', '-std=c++17', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                 unit.as_posix(), '-o', binary.as_posix()], capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run([binary.as_posix()], capture_output=True, text=True)
            self.assertEqual(run_result.returncode, 0, run_result.stderr)


if __name__ == '__main__':
    unittest.main()

