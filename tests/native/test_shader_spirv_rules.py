#!/usr/bin/env python3
"""Vulkan validation regressions for subgroup inputs and shared UBO types.

Run: python3 tests/native/test_shader_spirv_rules.py
Synthetic modules only; no game bytes are part of these fixtures.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/graphics'))
import build_ffrs_xgs as build
import fix_spirv_capabilities as caps
import prepare_ffrs_xlate as prepare

FRAGMENT = '''
OpCapability Shader
OpCapability GroupNonUniformBallot
OpCapability GroupNonUniformShuffle
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %lane %color
OpExecutionMode %main OriginUpperLeft
OpDecorate %lane BuiltIn SubgroupLocalInvocationId
OpDecorate %lane Flat
OpDecorate %color Location 0
%void = OpTypeVoid
%fn = OpTypeFunction %void
%uint = OpTypeInt 32 0
%float = OpTypeFloat 32
%v4 = OpTypeVector %float 4
%pin = OpTypePointer Input %uint
%pout = OpTypePointer Output %v4
%lane = OpVariable %pin Input
%color = OpVariable %pout Output
%main = OpFunction %void None %fn
%entry = OpLabel
%id = OpLoad %uint %lane
%value = OpConvertUToF %float %id
%rgba = OpCompositeConstruct %v4 %value %value %value %value
OpStore %color %rgba
OpReturn
OpFunctionEnd
'''

COMPUTE = '''
OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
OpExecutionMode %main LocalSize 1 1 1
OpDecorate %array ArrayStride 16
OpDecorate %block Block
OpMemberDecorate %block 0 Offset 0
OpDecorate %ubo DescriptorSet 0
OpDecorate %ubo Binding 0
%void = OpTypeVoid
%fn = OpTypeFunction %void
%uint = OpTypeInt 32 0
%eight = OpConstant %uint 8
%zero = OpConstant %uint 0
%one = OpConstant %uint 1
%array = OpTypeArray %uint %eight
%block = OpTypeStruct %array
%pu = OpTypePointer Uniform %block
%ubo = OpVariable %pu Uniform
%pw = OpTypePointer Workgroup %array
%shared = OpVariable %pw Workgroup
%pu_uint = OpTypePointer Uniform %uint
%pw_uint = OpTypePointer Workgroup %uint
%main = OpFunction %void None %fn
%entry = OpLabel
%src = OpAccessChain %pu_uint %ubo %zero %one
%value = OpLoad %uint %src
%dst = OpAccessChain %pw_uint %shared %one
OpStore %dst %value
OpReturn
OpFunctionEnd
'''


LAYER = '''
OpCapability Shader
OpCapability DrawParameters
OpCapability ShaderViewportIndexLayerEXT
OpExtension "SPV_EXT_shader_viewport_index_layer"
OpMemoryModel Logical GLSL450
OpEntryPoint Vertex %main "main" %instance %base %layer %position %value
OpDecorate %instance BuiltIn InstanceIndex
OpDecorate %base BuiltIn BaseInstance
OpDecorate %layer BuiltIn Layer
OpDecorate %position BuiltIn Position
OpDecorate %value Location 1
OpDecorate %block Block
OpMemberDecorate %block 0 Offset 1604
OpDecorate %ubo DescriptorSet 0
OpDecorate %ubo Binding 0
%void = OpTypeVoid
%fn = OpTypeFunction %void
%int = OpTypeInt 32 1
%float = OpTypeFloat 32
%v4 = OpTypeVector %float 4
%zero = OpConstant %int 0
%fzero = OpConstant %float 0
%fone = OpConstant %float 1
%pos = OpConstantComposite %v4 %fzero %fzero %fzero %fone
%pin = OpTypePointer Input %int
%pout = OpTypePointer Output %int
%pv4 = OpTypePointer Output %v4
%pf = OpTypePointer Output %float
%block = OpTypeStruct %int
%pu = OpTypePointer Uniform %block
%pui = OpTypePointer Uniform %int
%pp = OpTypePointer Private %int
%ubo = OpVariable %pu Uniform
%instance = OpVariable %pin Input
%base = OpVariable %pin Input
%layer = OpVariable %pout Output
%position = OpVariable %pv4 Output
%value = OpVariable %pf Output
%scratch = OpVariable %pp Private
%main = OpFunction %void None %fn
%entry = OpLabel
%i = OpLoad %int %instance
%b = OpLoad %int %base
%p = OpAccessChain %pui %ubo %zero
%driver = OpLoad %int %p
%relative = OpISub %int %i %b
%selected = OpIAdd %int %relative %driver
%bits = OpBitcast %float %selected
OpStore %layer %selected
OpStore %value %bits
OpStore %scratch %selected
OpStore %position %pos
OpReturn
OpFunctionEnd
'''


class ShaderRules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assembler = shutil.which('spirv-as')
        cls.validator = shutil.which('spirv-val')
        if not cls.assembler or not cls.validator:
            raise unittest.SkipTest('spirv-as and spirv-val are required for Vulkan regressions')

    def assemble(self, text):
        with tempfile.TemporaryDirectory() as directory:
            asm, spv = Path(directory) / 'input.spvasm', Path(directory) / 'input.spv'
            asm.write_text(text)
            subprocess.run([self.assembler, '--target-env', 'vulkan1.1', str(asm), '-o', str(spv)], check=True)
            return build.read_words(spv.read_bytes())

    def valid(self, words):
        return build.validate(build.write_words(words), self.validator)

    def fix_caps(self, words):
        declared = set(caps.analyse(words)['declared'])
        unused = build.provably_unused(words)
        trimmed, _ = build.trim_capabilities(words, shutil.which('spirv-opt'))
        return build.apply_device_fixes(trimmed, declared, unused)[0]

    def test_subgroup_builtin_requires_basic_even_without_group_operations(self):
        words = self.assemble(FRAGMENT)
        self.assertFalse(any(333 <= op <= 366 for _, op, _ in build.instructions(words)))
        bare, _, _, _ = caps.rewrite(words, [], [64, 65])
        self.assertFalse(self.valid(bare)[0])
        fixed = self.fix_caps(words)
        self.assertIn(61, caps.analyse(fixed)['declared'])
        self.assertIn(61, caps.analyse(fixed)['used'])
        self.assertNotIn(61, build.provably_unused(fixed))
        self.assertTrue(self.valid(fixed)[0], self.valid(fixed)[1])
        # Declaration repair cannot change arithmetic, loads, stores or flow.
        body = lambda w: [(op, operands) for _, op, operands in build.instructions(w) if op != 17]
        self.assertEqual(body(words), body(fixed))

    def test_shared_array_isolated_without_changing_ubo_layout_or_operations(self):
        words = self.assemble(COMPUTE)
        self.assertIn('explicit layout', self.valid(words)[1])
        before = build.spirv_info(words)
        fixed, report = build.isolate_workgroup_array_layout(words)
        self.assertEqual(len(report['cloned_arrays']), 1)
        change = report['cloned_arrays'][0]
        old, new = change['original_type'], change['workgroup_type']
        after = build.spirv_info(fixed)
        self.assertEqual(before['types'][old], after['types'][old])
        self.assertEqual(before['deco'][old], after['deco'][old])
        self.assertEqual(after['deco'][old][6], 16)
        self.assertNotIn(6, after['deco'][new])
        for pointer, (storage, pointee) in before['pointers'].items():
            if storage != 4:
                self.assertEqual(after['pointers'][pointer], (storage, pointee))
        ops = lambda w: [(op, operands) for _, op, operands in build.instructions(w) if op not in (28, 32)]
        self.assertEqual(ops(words), ops(fixed))
        self.assertTrue(self.valid(fixed)[0], self.valid(fixed)[1])
        self.assertEqual(build.isolate_workgroup_array_layout(fixed)[0], fixed)

    def test_whole_array_load_is_rejected_until_values_are_retyped(self):
        text = COMPUTE.replace('%entry = OpLabel', '%entry = OpLabel\n%all = OpLoad %array %shared')
        with self.assertRaisesRegex(ValueError, 'whole Workgroup array value'):
            build.isolate_workgroup_array_layout(self.assemble(text))

    def test_module_without_shared_layout_is_unchanged(self):
        words = self.assemble(FRAGMENT)
        self.assertEqual(build.isolate_workgroup_array_layout(words), (words, {'cloned_arrays': []}))

    def test_extra_array_annotation_is_not_silently_discarded(self):
        text = COMPUTE.replace('OpDecorate %array ArrayStride 16',
                               'OpDecorate %array ArrayStride 16\nOpDecorate %array RelaxedPrecision')
        with self.assertRaisesRegex(ValueError, 'additional Workgroup array decorations'):
            build.isolate_workgroup_array_layout(self.assemble(text))

    def test_full_extension_name_is_decoded_and_still_rejected(self):
        extension = b'SPV_EXT_shader_viewport_index_layer\0'
        extension += bytes(-len(extension) % 4)
        literal = list(struct.unpack('<%dI' % (len(extension) // 4), extension))
        words = [0x07230203, 0x00010300, 0, 1, 0, (len(literal) + 1) << 16 | 10, *literal]
        self.assertEqual(caps.analyse(words)['extensions'], ['SPV_EXT_shader_viewport_index_layer'])
        self.assertEqual(build.spirv_info(words)['exts'], ['SPV_EXT_shader_viewport_index_layer'])
        with self.assertRaisesRegex(ValueError, 'still declares an extension'):
            build.reflect('test', 'vs', b'', words, {}, 'nvn')

    def test_core_layer_promotion_updates_all_global_interfaces_and_preserves_operations(self):
        words = self.assemble(LAYER)
        self.assertTrue(self.valid(words)[0], self.valid(words)[1])
        promoted, report = build.promote_layer_output_core(words)
        self.assertEqual(promoted[1], 0x00010500)
        self.assertEqual(promoted[2:5], words[2:5])
        self.assertEqual(report['capability_replaced'], [5254, 69])
        self.assertEqual(set(caps.analyse(promoted)['declared']), {1, 69, 4427})
        self.assertEqual(caps.analyse(promoted)['extensions'], [])
        self.assertIn(69, caps.analyse(promoted)['used'])
        self.assertTrue(report['entry_interfaces'][0]['added_globals'])
        interface = report['entry_interfaces'][0]
        self.assertEqual(set(report['global_variables']), set(interface['original'] + interface['added_globals']))
        unchanged = lambda w: [(op, operands) for _, op, operands in build.instructions(w) if op not in (10, 15, 17)]
        self.assertEqual(unchanged(words), unchanged(promoted))
        self.assertTrue(build.validate(build.write_words(promoted), self.validator, 'vulkan1.2')[0])
        self.assertFalse(self.valid(promoted)[0])  # old Vulkan1.1 cannot consume SPIR-V1.5

    def test_one_word_version_and_cap_change_without_interface_repair_is_invalid(self):
        words = self.assemble(LAYER)
        promoted, _ = build.promote_layer_output_core(words)
        old_entry = next(operands for _, op, operands in build.instructions(words) if op == 15)
        broken = list(promoted[:5])
        for _, op, operands in build.instructions(promoted):
            if op == 15: operands = old_entry
            broken.extend([(len(operands) + 1) << 16 | op, *operands])
        valid, first, _ = build.validate(build.write_words(broken), self.validator, 'vulkan1.2')
        self.assertFalse(valid, first)

    def test_layer_feature_target_contract_defaults_disabled_and_checks_each_field(self):
        words, _ = build.promote_layer_output_core(self.assemble(LAYER))
        with self.assertRaisesRegex(ValueError, 'explicit Vulkan1.2'):
            build.require_output_features(words)
        build.require_output_features(words, build.CORE_LAYER_TARGET)
        for field, bad in [('instance_api_version', (1 << 22) | (1 << 12)),
                           ('physical_api_version', (1 << 22) | (1 << 12)), ('shaderOutputLayer', False)]:
            target = dict(build.CORE_LAYER_TARGET, **{field: bad})
            with self.assertRaises(ValueError): build.require_output_features(words, target)

    def test_exporter_rejects_core_layer_in_an_unreviewed_stage_before_resource_decode(self):
        words, _ = build.promote_layer_output_core(self.assemble(LAYER))
        with self.assertRaisesRegex(ValueError, 'reviewed vertex stage'):
            build.reflect('test', 'ps', b'', words, {}, 'nvn', build.CORE_LAYER_TARGET)

    def test_core_viewport_index_requires_its_own_enabled_target_feature(self):
        words, _ = build.promote_layer_output_core(self.assemble(LAYER))
        changed = [*words[:5]]
        for _, op, operands in build.instructions(words):
            if op == 17 and operands == [69]: operands = [70]
            changed.extend([(len(operands) + 1) << 16 | op, *operands])
        with self.assertRaises(ValueError):
            build.require_output_features(changed, dict(build.CORE_LAYER_TARGET, shaderOutputViewportIndex=False))

    def test_layer_promotion_rejects_unknown_extension_and_viewport_output(self):
        for text in [LAYER.replace('SPV_EXT_shader_viewport_index_layer', 'SPV_FAKE_unknown_layer'),
                     LAYER.replace('BuiltIn Layer', 'BuiltIn ViewportIndex')]:
            with self.assertRaises(ValueError): build.promote_layer_output_core(self.assemble(text))

    def test_capability_rewriter_will_not_drop_real_layer_output_requirement(self):
        legacy = self.assemble(LAYER)
        self.assertIn(5254, caps.analyse(legacy)['used'])
        with self.assertRaises(SystemExit): caps.rewrite(legacy, [], [5254])
        promoted, _ = build.promote_layer_output_core(legacy)
        with self.assertRaises(SystemExit): caps.rewrite(promoted, [], [69])
        fixed = self.fix_caps(promoted)
        self.assertIn(69, caps.analyse(fixed)['declared'])
        self.assertTrue(build.validate(build.write_words(fixed), self.validator, 'vulkan1.2')[0])

    def test_private_accessor_patch_is_narrow_and_refuses_source_drift(self):
        original = 'before\n' + prepare.ANCHOR + 'after\n'
        patched = prepare.patch_program(original)
        self.assertEqual(patched.replace(prepare.INSERT, ''), original)
        self.assertIn('QueryHostSupportsNonConstantTextureOffset() => false', patched)
        with self.assertRaises(ValueError):
            prepare.patch_program(patched)
        with self.assertRaises(ValueError):
            prepare.patch_program('unexpected upstream')


if __name__ == '__main__':
    unittest.main(verbosity=2)
