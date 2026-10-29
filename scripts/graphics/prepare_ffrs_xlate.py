#!/usr/bin/env python3
"""Prepare a private, attributable Vulkan translator; never edit the reference.

Example (all outputs stay in the project's .local directory):
  prepare_ffrs_xlate.py --source .local/refs/nxnative --out .local/ffrs-xlate \
      --dotnet /path/to/sdk/dotnet --build
  build_ffrs_xgs.py --keys CONTENT_KEY --translator .local/ffrs-xlate/bin/dmsh_xlate \
      --dotnet-root /path/to/sdk --keep-work

The baseline patch disables nonconstant texture Offset operands for Vulkan.
--direct-boolean-proof adds bounded canonical 0/-1 And/Not predicate inference.
--direct-static-proof additionally enables bounded, use-guarded Phi projection
before the existing static texture-handle matcher. Unknown or cyclic provenance
is refused; no texture operation, coordinate or result is discarded. The private
manifest records revision, source hashes, exact patch, licenses and command.
"""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

RYU = Path('dm/third_party/ryujinx')
TOOL = Path('dm/gfx/tools/dmsh_xlate')
PROJECTS = ('Ryujinx.Common', 'Ryujinx.Graphics.Shader', 'Spv.Generator')
ANCHOR = '        public int QueryTextureArrayLengthFromPool() => 32;\n'
INSERT = ('        // Vulkan only permits a dynamic Offset on gather operations.\n'
          '        // TexturePass preserves other offsets through coordinate arithmetic.\n'
          '        public bool QueryHostSupportsNonConstantTextureOffset() => false;\n')
OPTIMIZATIONS = RYU / 'src/Ryujinx.Graphics.Shader/Translation/Optimizations'
HANDLE_PROOF_SOURCE = '''using Ryujinx.Graphics.Shader.IntermediateRepresentation;
using System.Collections.Generic;

namespace Ryujinx.Graphics.Shader.Translation.Optimizations
{
    // Projection is local to one texture use. Original IR nodes are not edited.
    // Facts come only from mandatory CFG edges and the same SSA predicate.
    static class HandleUseProof
    {
        private const int MaxDepth = 32;
        private const int MaxNodes = 256;
        private const int MaxEdges = 128;

        public sealed class Snapshot
        {
            public Operand[] Sources, Dests;
            public Instruction Inst;
            public SamplerType Type;
            public TextureFormat Format;
            public TextureFlags Flags;
            public int Component;
        }

        public static Snapshot Capture(TextureOperation operation)
        {
            if (System.Environment.GetEnvironmentVariable("DMSH_HANDLE_PROOF_AUDIT") == null) return null;
            Snapshot s = new() { Sources = new Operand[operation.SourcesCount], Dests = new Operand[operation.DestsCount],
                Inst = operation.Inst, Type = operation.Type, Format = operation.Format, Flags = operation.Flags, Component = operation.Index };
            for (int i = 0; i < s.Sources.Length; i++) s.Sources[i] = operation.GetSource(i);
            for (int i = 0; i < s.Dests.Length; i++) s.Dests[i] = operation.GetDest(i);
            return s;
        }

        public static void Audit(Snapshot before, TextureOperation after, bool dropped)
        {
            if (before == null) return;
            bool directStatic = !dropped && after.SourcesCount == before.Sources.Length - 1;
            bool coordinates = directStatic;
            if (directStatic)
                for (int i = 1; i < before.Sources.Length; i++) coordinates &= before.Sources[i] == after.GetSource(i - 1);
            bool results = after.DestsCount == before.Dests.Length;
            if (results) for (int i = 0; i < before.Dests.Length; i++) results &= before.Dests[i] == after.GetDest(i);
            bool unchanged = before.Inst == after.Inst && before.Type == after.Type && before.Format == after.Format && before.Component == after.Index &&
                after.Flags == (before.Flags & ~TextureFlags.Bindless);
            string line = System.Text.Json.JsonSerializer.Serialize(new {
                shader = System.Environment.GetEnvironmentVariable("DMSH_HANDLE_PROOF_KEY"),
                kind = dropped ? "dropped" : directStatic ? "static" : "array",
                instruction = before.Inst.ToString(), type = before.Type.ToString(),
                coordinate_references_preserved = coordinates, result_references_preserved = results,
                type_format_component_flags_preserved = unchanged, after.Binding });
            string path = System.Environment.GetEnvironmentVariable("DMSH_HANDLE_PROOF_AUDIT") + "." + System.Environment.ProcessId + ".jsonl";
            System.IO.File.AppendAllText(path, line + System.Environment.NewLine);
            if (directStatic && (!coordinates || !results || !unchanged))
                throw new System.InvalidOperationException("Static handle projection changed texture coordinates, result, type or flags");
        }

        public static bool TryResolve(Operand source, BasicBlock use, out Operand result)
        {
            result = null;
            Dictionary<Operand, bool> facts = new();
            int edges = MaxEdges;
            if (!CollectFacts(use, facts, ref edges)) return false;
            int nodes = MaxNodes;
            return Resolve(source, use, facts, new HashSet<Operand>(), 0, ref nodes, out result);
        }

        private static bool AddEdgeFact(BasicBlock from, BasicBlock to, Dictionary<Operand, bool> facts)
        {
            if (from.Next != to && from.Branch != to) return false;
            if (from.Next == to && from.Branch == to) return true;
            if (from.GetLastOp() is not Operation branch ||
                (branch.Inst != Instruction.BranchIfTrue && branch.Inst != Instruction.BranchIfFalse)) return true;
            Operand condition = branch.GetSource(0);
            bool truth = (from.Branch == to) == (branch.Inst == Instruction.BranchIfTrue);
            HashSet<Operand> seen = new();
            for (int depth = 0; condition.AsgOp is Operation op; depth++)
            {
                if (depth >= MaxDepth || !seen.Add(condition)) return false;
                if (op.Inst == Instruction.Copy) condition = op.GetSource(0);
                else if (op.Inst == Instruction.LogicalNot) { condition = op.GetSource(0); truth = !truth; }
                else break;
            }
            if (facts.TryGetValue(condition, out bool prior) && prior != truth) return false;
            facts[condition] = truth;
            return true;
        }

        private static bool CollectFacts(BasicBlock block, Dictionary<Operand, bool> facts, ref int budget)
        {
            HashSet<BasicBlock> seen = new();
            while (block.Predecessors.Count == 1)
            {
                if (--budget < 0 || !seen.Add(block)) return false;
                BasicBlock predecessor = block.Predecessors[0];
                // Do not transport a predicate across a backedge/iteration.
                if (predecessor.Index >= block.Index) return false;
                if (!AddEdgeFact(predecessor, block, facts)) return false;
                block = predecessor;
            }
            return true;
        }

        private static BasicBlock PhiBlock(PhiNode phi, BasicBlock use)
        {
            if (phi.SourcesCount == 0) return null;
            // Ryujinx Phi.GetBlock stores the reaching DEFINITION block, not
            // the incoming predecessor. Find the owner in the real CFG.
            Queue<BasicBlock> pending = new();
            HashSet<BasicBlock> seen = new();
            pending.Enqueue(use);
            int budget = MaxEdges;
            while (pending.Count != 0 && --budget >= 0)
            {
                BasicBlock block = pending.Dequeue();
                if (!seen.Add(block)) continue;
                if (block.Operations.Contains(phi)) return block;
                foreach (BasicBlock predecessor in block.Predecessors) pending.Enqueue(predecessor);
            }
            return null;
        }

        private static bool IncomingValue(PhiNode phi, BasicBlock predecessor, out Operand value)
        {
            value = null;
            HashSet<BasicBlock> seen = new();
            // Ssa.FindDefinition uses this same immediate-dominator walk.
            // The nearest definition recorded by this Phi is the one reaching
            // the actual incoming edge; missing/stale metadata refuses proof.
            for (int depth = 0; predecessor != null && depth < MaxEdges && seen.Add(predecessor); depth++)
            {
                for (int i = 0; i < phi.SourcesCount; i++)
                    if (phi.GetBlock(i) == predecessor) { value = phi.GetSource(i); return true; }
                if (predecessor.ImmediateDominator == predecessor) break;
                predecessor = predecessor.ImmediateDominator;
            }
            return false;
        }

        private static bool Same(Operand a, Operand b, ref int budget, int depth = 0)
        {
            if (--budget < 0) return false;
            if (a == b) return true;
            if (depth >= MaxDepth || a.Type != b.Type) return false;
            if (a.Type == OperandType.Constant || a.Type == OperandType.ConstantBuffer) return a.Value == b.Value;
            if (a.AsgOp is not Operation x || b.AsgOp is not Operation y ||
                x.Inst != Instruction.BitwiseOr || y.Inst != Instruction.BitwiseOr) return false;
            return Same(x.GetSource(0), y.GetSource(0), ref budget, depth + 1) && Same(x.GetSource(1), y.GetSource(1), ref budget, depth + 1) ||
                   Same(x.GetSource(0), y.GetSource(1), ref budget, depth + 1) && Same(x.GetSource(1), y.GetSource(0), ref budget, depth + 1);
        }

        private static bool Resolve(Operand source, BasicBlock use, Dictionary<Operand, bool> facts, HashSet<Operand> active,
                                    int depth, ref int budget, out Operand result)
        {
            result = null;
            if (--budget < 0 || depth >= MaxDepth || source == null || source.Type == OperandType.Undefined || !active.Add(source)) return false;
            try
            {
                if (source.AsgOp is PhiNode phi)
                {
                    BasicBlock block = PhiBlock(phi, use);
                    if (block == null || block.Predecessors.Count == 0) return false;
                    Operand unique = null;
                    foreach (BasicBlock predecessor in new HashSet<BasicBlock>(block.Predecessors))
                    {
                        if (predecessor.Index >= block.Index || !IncomingValue(phi, predecessor, out Operand incomingValue)) return false;
                        Dictionary<Operand, bool> incoming = new();
                        int edges = MaxEdges;
                        if (!CollectFacts(predecessor, incoming, ref edges) || !AddEdgeFact(predecessor, block, incoming)) return false;
                        bool impossible = false;
                        foreach (var pair in incoming)
                            if (facts.TryGetValue(pair.Key, out bool truth) && truth != pair.Value) { impossible = true; break; }
                        if (impossible) continue;
                        // Use facts remain fixed at the actual use; do not infer
                        // another path's predicate or ignore feasible undef.
                        if (!Resolve(incomingValue, use, facts, active, depth + 1, ref budget, out Operand value)) return false;
                        int comparison = MaxNodes;
                        if (unique != null && !Same(unique, value, ref comparison)) return false;
                        unique = value;
                    }
                    result = unique;
                    return unique != null;
                }
                if (source.AsgOp is Operation operation)
                {
                    if (operation.Inst == Instruction.Copy)
                        return Resolve(operation.GetSource(0), use, facts, active, depth + 1, ref budget, out result);
                    if (operation.Inst == Instruction.BitwiseOr)
                    {
                        if (!Resolve(operation.GetSource(0), use, facts, active, depth + 1, ref budget, out Operand a) ||
                            !Resolve(operation.GetSource(1), use, facts, active, depth + 1, ref budget, out Operand b)) return false;
                        if (a == operation.GetSource(0) && b == operation.GetSource(1)) result = source;
                        else
                        {
                            // A proof-only expression may contain only immutable
                            // leaves: never add phantom uses of live IR locals.
                            if ((a.Type != OperandType.ConstantBuffer && a.Type != OperandType.Constant) ||
                                (b.Type != OperandType.ConstantBuffer && b.Type != OperandType.Constant)) return false;
                            result = new Operand(OperandType.LocalVariable);
                            _ = new Operation(Instruction.BitwiseOr, result, a, b);
                        }
                        return true;
                    }
                }
                // Non-Phi nodes keep their identity. The existing static-handle
                // matcher still checks the supported CB/mask/shift patterns;
                // an input/storage load is never promoted to a static binding.
                result = source;
                return true;
            }
            finally { active.Remove(source); }
        }
    }
}
'''


# Opt-in v4 extension. The frozen v3 generator path stays byte-identical.
BOOLEAN_PROOF_HELPERS = """
        private const int MaxBooleanAtoms = 8;

        // Branch facts mean nonzero/zero. They imply -1/0 only for values whose
        // complete expression is independently proved canonical. In particular,
        // arbitrary integer AND/NOT must never become Boolean reasoning.
        private static bool CanonicalBoolean(Operand source, HashSet<Operand> active,
                                             HashSet<Operand> atoms, int depth, ref int budget)
        {
            if (--budget < 0 || depth >= MaxDepth || source == null || !active.Add(source)) return false;
            try
            {
                if (source.Type == OperandType.Constant) return source.Value == 0 || source.Value == -1;
                if (source.AsgOp is not Operation operation) return false;
                if (operation.Inst.IsComparison())
                    return operation.SourcesCount == 2 && (atoms.Add(source) || atoms.Contains(source)) && atoms.Count <= MaxBooleanAtoms;
                if (operation.Inst == Instruction.Copy || operation.Inst == Instruction.BitwiseNot)
                    return operation.SourcesCount == 1 && CanonicalBoolean(operation.GetSource(0), active, atoms, depth + 1, ref budget);
                if (operation.Inst == Instruction.BitwiseAnd)
                    return operation.SourcesCount == 2 &&
                           CanonicalBoolean(operation.GetSource(0), active, atoms, depth + 1, ref budget) &&
                           CanonicalBoolean(operation.GetSource(1), active, atoms, depth + 1, ref budget);
                return false;
            }
            finally { active.Remove(source); }
        }

        // Called only after the entire bitwise expression is canonical. Unknown
        // comparisons are not evaluated from uniforms or assumed true/false.
        private static bool EvaluateBoolean(Operand source, Dictionary<Operand, bool> facts,
                                            HashSet<Operand> active, int depth, ref int budget, out bool truth)
        {
            truth = false;
            if (--budget < 0 || depth >= MaxDepth || !active.Add(source)) return false;
            try
            {
                if (facts.TryGetValue(source, out truth)) return true;
                if (source.Type == OperandType.Constant) { truth = source.Value != 0; return true; }
                if (source.AsgOp is not Operation operation) return false;
                if (operation.Inst == Instruction.Copy)
                    return EvaluateBoolean(operation.GetSource(0), facts, active, depth + 1, ref budget, out truth);
                if (operation.Inst == Instruction.BitwiseNot)
                {
                    if (!EvaluateBoolean(operation.GetSource(0), facts, active, depth + 1, ref budget, out bool value)) return false;
                    truth = !value; return true;
                }
                if (operation.Inst == Instruction.BitwiseAnd)
                {
                    bool knownA = EvaluateBoolean(operation.GetSource(0), facts, active, depth + 1, ref budget, out bool a);
                    bool knownB = EvaluateBoolean(operation.GetSource(1), facts, active, depth + 1, ref budget, out bool b);
                    if (budget < 0) return false;
                    if (knownA && !a || knownB && !b) { truth = false; return true; }
                    if (knownA && knownB) { truth = a && b; return true; }
                }
                return false;
            }
            finally { active.Remove(source); }
        }

        private static bool KnownUseTruth(Operand source, Dictionary<Operand, bool> facts, out bool truth)
        {
            // Exact same-SSA facts remain valid for any integer branch value.
            if (facts.TryGetValue(source, out truth)) return true;
            int budget = MaxNodes;
            if (!CanonicalBoolean(source, new HashSet<Operand>(), new HashSet<Operand>(), 0, ref budget)) return false;
            return EvaluateBoolean(source, facts, new HashSet<Operand>(), 0, ref budget, out truth);
        }
"""


def boolean_proof_source():
    anchor = '        private static bool AddEdgeFact('
    source = HANDLE_PROOF_SOURCE.replace(anchor, BOOLEAN_PROOF_HELPERS + '\n' + anchor, 1)
    old = 'if (facts.TryGetValue(pair.Key, out bool truth) && truth != pair.Value)'
    if source.count(old) != 1:
        raise ValueError('static proof changed; review canonical Boolean inference')
    return source.replace(old, 'if (KnownUseTruth(pair.Key, facts, out bool truth) && truth != pair.Value)')


def patch_bindless(source):
    changes = {
        '                bindlessHandle = Utils.FindLastOperation(bindlessHandle, block);':
        '                if (!HandleUseProof.TryResolve(bindlessHandle, block, out bindlessHandle)) return false;',
        '                Operand src0 = Utils.FindLastOperation(handleCombineOp.GetSource(0), block);\n'
        '                Operand src1 = Utils.FindLastOperation(handleCombineOp.GetSource(1), block);':
        '                if (!HandleUseProof.TryResolve(handleCombineOp.GetSource(0), block, out Operand src0) ||\n'
        '                    !HandleUseProof.TryResolve(handleCombineOp.GetSource(1), block, out Operand src1)) return false;',
        '                Operand src0 = Utils.FindLastOperation(texOp.GetSource(0), block);':
        '                if (!HandleUseProof.TryResolve(texOp.GetSource(0), block, out Operand src0)) return false;',
    }
    for old, new in changes.items():
        if source.count(old) != 1:
            raise ValueError('upstream bindless matcher changed; review the guarded-Phi patch')
        source = source.replace(old, new)
    before = '                if (!TryConvertBindless(block, resourceManager, gpuAccessor, texOp) &&'
    source = source.replace(before, '                var proofSnapshot = HandleUseProof.Capture(texOp);\n' + before)
    before = '                    Utils.DeleteNode(node, texOp);\n                }\n            }\n        }\n\n        private static bool GenerateBindlessAccess('
    after = ('                    HandleUseProof.Audit(proofSnapshot, texOp, true);\n'
             '                    Utils.DeleteNode(node, texOp);\n                }\n'
             '                else HandleUseProof.Audit(proofSnapshot, texOp, false);\n'
             '            }\n        }\n\n        private static bool GenerateBindlessAccess(')
    if source.count(before) != 1:
        raise ValueError('upstream bindless pass changed; review the preservation audit hook')
    source = source.replace(before, after)
    return source


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def patch_program(source, direct_static_proof=False):
    if source.count(ANCHOR) != 1 or 'public bool QueryHostSupportsNonConstantTextureOffset()' in source:
        raise ValueError('unexpected upstream accessor; review the offset fallback patch')
    source = source.replace(ANCHOR, ANCHOR + INSERT)
    if direct_static_proof:
        anchor = '            string key = (string)job["key"];\n'
        if source.count(anchor) != 1:
            raise ValueError('upstream job loop changed; review audit attribution')
        source = source.replace(anchor, anchor +
            '            if (Environment.GetEnvironmentVariable("DMSH_HANDLE_PROOF_AUDIT") != null)\n'
            '                Environment.SetEnvironmentVariable("DMSH_HANDLE_PROOF_KEY", key);\n')
    return source


def prepare(source, out, direct_static_proof=False, direct_boolean_proof=False):
    direct_static_proof = direct_static_proof or direct_boolean_proof
    source, out = Path(source).resolve(), Path(out).resolve()
    if out.exists():
        raise ValueError('output already exists; choose a new private directory: %s' % out)
    if out == source or source in out.parents:
        raise ValueError('private output must be outside the read-only reference tree')
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    tracked_changes = subprocess.check_output(
        ['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no'], text=True)
    if tracked_changes:
        raise ValueError('upstream has tracked changes; preserve them and review source identity first')
    files = [Path('LICENSE'), RYU / 'LICENSE.txt', RYU / 'Directory.Build.props',
             RYU / 'Directory.Packages.props', RYU / 'src/Spv.Generator/LICENSE']
    for directory in (TOOL, *(RYU / 'src' / project for project in PROJECTS)):
        files.extend(p.relative_to(source) for p in (source / directory).rglob('*')
                     if p.is_file() and not any(part in ('bin', 'obj', '.git') for part in p.parts)
                     and not p.name.startswith('._'))
    files = sorted(set(files))
    # Check every input before creating the destination.
    for relative in files:
        if not (source / relative).is_file():
            raise ValueError('required upstream source is missing: %s' % relative)
    changes = {TOOL / 'Program.cs': patch_program((source / TOOL / 'Program.cs').read_text(), direct_static_proof)}
    if direct_static_proof:
        changes[OPTIMIZATIONS / 'BindlessElimination.cs'] = patch_bindless(
            (source / OPTIMIZATIONS / 'BindlessElimination.cs').read_text())
        changes[OPTIMIZATIONS / 'HandleUseProof.cs'] = boolean_proof_source() if direct_boolean_proof else HANDLE_PROOF_SOURCE
    out.mkdir(parents=True)
    inventory = []
    for relative in files:
        destination = out / 'src' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / relative, destination)
        inventory.append({'path': str(relative), 'upstream_sha256': digest(source / relative)})
    patch = ''
    for relative, patched in changes.items():
        (out / 'src' / relative).write_text(patched)
        original = (source / relative).read_text() if (source / relative).exists() else ''
        patch += ''.join(difflib.unified_diff(original.splitlines(True), patched.splitlines(True),
                                            fromfile='a/' + str(relative) if original else '/dev/null',
                                            tofile='b/' + str(relative)))
        if not (source / relative).exists():
            inventory.append({'path': str(relative), 'upstream_sha256': None})
    (out / 'source.patch').write_text(patch)
    for row in inventory:
        row['private_sha256'] = digest(out / 'src' / row['path'])
    manifest = {'upstream': str(source), 'upstream_revision': revision,
                'upstream_tracked_changes': tracked_changes,
                'patch': 'source.patch', 'patch_sha256': digest(out / 'source.patch'),
                'files': inventory,
                'licenses': ['src/LICENSE', 'src/' + str(RYU / 'LICENSE.txt'),
                             'src/' + str(RYU / 'src/Spv.Generator/LICENSE')],
                'patch_scope': ('Vulkan offset fallback and bounded use-guarded Phi projection'
                                if direct_static_proof else 'Vulkan nonconstant texture offset IR fallback only'),
                'direct_static_proof': direct_static_proof,
                'direct_boolean_proof': direct_boolean_proof,
                'generator': str(Path(__file__).resolve()),
                'generator_sha256': digest(__file__),
                'prepare_argv': sys.argv}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def build(out, dotnet, manifest):
    out, dotnet = Path(out).resolve(), Path(dotnet).resolve()
    command = [str(dotnet), 'publish', str(out / 'src' / TOOL / 'dmsh_xlate.csproj'),
               '-c', 'Release', '--self-contained', 'false', '-o', str(out / 'bin'),
               '-p:RyuSrc=' + str(out / 'src' / RYU)]
    env = dict(os.environ, DOTNET_ROOT=str(dotnet.parent), DOTNET_CLI_TELEMETRY_OPTOUT='1',
               DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1')
    manifest['build_command'] = command
    manifest['build_cwd'] = str(out)
    manifest['dotnet_info'] = subprocess.check_output([str(dotnet), '--info'], text=True, env=env)
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    # The log and exact command are durable before the long-running build starts.
    with (out / 'build.log').open('w') as log:
        result = subprocess.run(command, cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT)
    manifest['build_exit_code'] = result.returncode
    if result.returncode == 0:
        manifest['binaries'] = {str(p.relative_to(out)): digest(p) for p in (out / 'bin').iterdir()
                               if p.is_file()}
        manifest['embedded_source_sha'] = subprocess.check_output(
            [str(out / 'bin/dmsh_xlate'), '--source-sha'], text=True, env=env).strip()
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--dotnet', type=Path, help='local .NET SDK executable')
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--direct-static-proof', action='store_true',
                        help='enable bounded use-guarded Phi projection before static handle matching')
    parser.add_argument('--direct-boolean-proof', action='store_true',
                        help='also prove bounded canonical 0/-1 And/Not predicates at the texture use')
    args = parser.parse_args()
    if args.build and not args.dotnet:
        parser.error('--build requires --dotnet')
    manifest = prepare(args.source, args.out, args.direct_static_proof, args.direct_boolean_proof)
    print('private translator sources: %s' % args.out.resolve(), flush=True)
    return build(args.out, args.dotnet, manifest) if args.build else 0


if __name__ == '__main__':
    sys.exit(main())
