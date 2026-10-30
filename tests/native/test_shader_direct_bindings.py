#!/usr/bin/env python3
"""Exercise the real private translator DLL against an independent CFG oracle.

Requires the private fork prepared with --direct-static-proof and a local SDK.
All generated fixtures/logs are private; no game shader is embedded here.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / '.local/codex-shader-coverage'
FORK = Path(os.environ.get('FFRS_DIRECT_FORK', str(BASE / 'translator/direct-boolean-proof-v4')))
DOTNET = Path(os.environ.get('FFRS_DOTNET', str(BASE / 'dotnet-sdk/dotnet')))

FIXTURE = r'''
using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

class Program
{
    static Assembly A;
    static Type T(string n) => A.GetType("Ryujinx.Graphics.Shader.IntermediateRepresentation." + n, false) ?? A.GetType("Ryujinx.Graphics.Shader.Translation." + n, false) ?? A.GetType("Ryujinx.Graphics.Shader." + n, true);
    static object E(string t, string v) => Enum.Parse(T(t), v);
    static object New(string t, params object[] args) => Activator.CreateInstance(T(t), args);
    static object Get(object x, string p) { var property=x.GetType().GetProperty(p);return property!=null?property.GetValue(x):x.GetType().GetField(p).GetValue(x); }
    static void Set(object x, string p, object v) => x.GetType().GetProperty(p).SetValue(x, v);
    static object Call(object x, string m, params object[] args) => x.GetType().GetMethod(m).Invoke(x, args);
    static object Local() => New("Operand", E("OperandType", "LocalVariable"));
    static object Cb(int slot, int word) => New("Operand", slot, word);
    static object Const(int n) => New("Operand", E("OperandType", "Constant"), n);
    static object Op(string kind, object dest, params object[] sources)
    {
        Array typed = Array.CreateInstance(T("Operand"), sources.Length);
        for (int i = 0; i < sources.Length; i++) typed.SetValue(sources[i], i);
        return New("Operation", E("Instruction", kind), dest, typed);
    }
    static void Require(bool condition, string message) { if (!condition) throw new Exception(message); }
    class Block
    {
        public object Native;
        public int Index;
        public Block Next, Branch;
        public object Predicate;
        public bool BranchOnTrue = true;
        public List<Action<Dictionary<object,uint>,Block,int>> Actions = new();
        public List<Block> Pred = new();
        public Block(int i) { Index = i; Native = New("BasicBlock", i); }
        public void Link(Block next, Block branch = null, object predicate = null, bool branchOnTrue = true)
        {
            Next = next; Branch = branch; Predicate = predicate; BranchOnTrue = branchOnTrue;
            if (next != null) { Set(Native, "Next", next.Native); next.Pred.Add(this); }
            if (branch != null) { Set(Native, "Branch", branch.Native); branch.Pred.Add(this); }
            if (predicate != null) Call(Native, "Append", Op(branchOnTrue ? "BranchIfTrue" : "BranchIfFalse", null, predicate));
        }
    }
    class Graph
    {
        public List<Block> Blocks = new();
        public object P = Local(), Q = Local();
        public Block Entry, Use;
        public object Handle;
        public Dictionary<object,(string Kind,object[] Args)> Values = new();
        public Dictionary<Dictionary<object,uint>,Dictionary<object,object>> DeferredPhi = new();
        public Block Block() { var b = new Block(Blocks.Count); Blocks.Add(b); Entry ??= b; return b; }
        public object Value(Block block, string kind, params object[] args)
        {
            object v = Local();
            object operation=Op(kind,v,args);
            if(kind=="Load") {
                Array typed=Array.CreateInstance(T("Operand"),args.Length);
                for(int i=0;i<args.Length;i++)typed.SetValue(args[i],i);
                operation=New("Operation",E("Instruction",kind),E("StorageKind","Input"),v,typed);
            }
            Call(block.Native, "Append", operation);
            Values[v] = (kind, args);
            block.Actions.Add((env, previous, seed) => env[v] = Eval(v, env, seed, false));
            return v;
        }
        public object Phi(Block owner, params (Block Pred, object Value, Block Definition)[] incoming)
        {
            object v = Local(), phi = New("PhiNode", v);
            foreach (var edge in incoming) Call(phi, "AddSource", edge.Definition.Native, edge.Value);
            Call(owner.Native, "Append", phi);
            // The oracle uses actual incoming edges, independently of the
            // definition-block annotations consumed by HandleUseProof.
            owner.Actions.Insert(0, (env, previous, seed) => {
                var edge = incoming.Single(x => x.Pred == previous);
                // Select the executed edge now, but evaluate at the actual use.
                // A dead-path undef may be selected before a later guard exits.
                if(!DeferredPhi.TryGetValue(env,out var selected))DeferredPhi[env]=selected=new();
                selected[v]=edge.Value;
            });
            return v;
        }
        public uint Eval(object v, Dictionary<object,uint> env, int seed, bool useEnv = true)
        {
            if (useEnv && DeferredPhi.TryGetValue(env,out var selected) && selected.TryGetValue(v,out var incoming))return Eval(incoming,env,seed);
            if (useEnv && env.TryGetValue(v, out uint saved)) return saved;
            string type = Get(v, "Type").ToString();
            if (type == "ConstantBuffer") {
                uint bits = unchecked((uint)(int)Get(v, "Value"));
                return unchecked((bits ^ (uint)seed * 0x9e3779b9u) * 0x85ebca6bu + 0xc2b2ae35u);
            }
            if (type == "Constant") return unchecked((uint)(int)Get(v, "Value"));
            if (type == "Undefined") throw new InvalidOperationException("feasible undef");
            if (Values.TryGetValue(v, out var expression)) {
                if (expression.Kind == "Copy") return Eval(expression.Args[0], env, seed);
                if (expression.Kind == "CompareNotEqual") return Eval(expression.Args[0],env,seed)!=Eval(expression.Args[1],env,seed)?0xffffffffu:0u;
                if (expression.Kind == "CompareEqual") return Eval(expression.Args[0],env,seed)==Eval(expression.Args[1],env,seed)?0xffffffffu:0u;
                if (expression.Kind == "BitwiseAnd") return Eval(expression.Args[0],env,seed)&Eval(expression.Args[1],env,seed);
                if (expression.Kind == "BitwiseNot") return ~Eval(expression.Args[0],env,seed);
                if (expression.Kind == "BitwiseOr") return Eval(expression.Args[0], env, seed) | Eval(expression.Args[1], env, seed);
                if (expression.Kind == "Load") return unchecked((uint)seed * 0x27d4eb2du);
            }
            object op = Get(v, "AsgOp");
            if (op != null && op.GetType().Name == "Operation") {
                string kind = Get(op, "Inst").ToString();
                if (kind == "BitwiseOr") return Eval(Call(op,"GetSource",0),env,seed) | Eval(Call(op,"GetSource",1),env,seed);
                if (kind == "Copy") return Eval(Call(op,"GetSource",0),env,seed);
            }
            throw new Exception("oracle has no value for " + type);
        }
        public void Dominators()
        {
            var all = new HashSet<Block>(Blocks);
            var dom = Blocks.ToDictionary(b => b, b => b == Entry ? new HashSet<Block>{b} : new HashSet<Block>(all));
            bool changed;
            do {
                changed = false;
                foreach (var b in Blocks.Where(b => b != Entry)) {
                    var d = b.Pred.Count == 0 ? new HashSet<Block>() : new HashSet<Block>(dom[b.Pred[0]]);
                    foreach (var p in b.Pred.Skip(1)) d.IntersectWith(dom[p]);
                    d.Add(b);
                    if (!d.SetEquals(dom[b])) { dom[b] = d; changed = true; }
                }
            } while (changed);
            foreach (var b in Blocks) {
                var parent = b == Entry ? b : dom[b].Where(x => x != b).OrderByDescending(x => dom[x].Count).FirstOrDefault();
                Set(b.Native,"ImmediateDominator",parent?.Native);
            }
        }
        public IEnumerable<uint> Execute(int seed)
        {
            for (int p = 0; p < 2; p++) for (int q = 0; q < 2; q++) {
                var env = new Dictionary<object,uint>{{P,(uint)p},{Q,(uint)q}};
                Block current=Entry, previous=null;
                for(int steps=0;current!=null && steps<1024;steps++) {
                    foreach(var action in current.Actions) action(env,previous,seed);
                    if(current==Use) { yield return Eval(Handle,env,seed); break; }
                    var next = current.Predicate == null ? current.Next :
                        ((env[current.Predicate]!=0)==current.BranchOnTrue ? current.Branch : current.Next);
                    previous=current;current=next;
                }
            }
        }
        public int CheckOracle(object result)
        {
            int checks=0;
            for(int seed=1;seed<=64;seed++) for(int p=0;p<2;p++) for(int q=0;q<2;q++) {
                var env=new Dictionary<object,uint>{{P,(uint)p},{Q,(uint)q}};
                Block current=Entry,previous=null;
                for(int step=0;current!=null&&step<1024;step++) {
                    foreach(var action in current.Actions) action(env,previous,seed);
                    if(current==Use) {
                        Require(Eval(Handle,env,seed)==Eval(result,env,seed),"projected value differs on a reachable path");checks++;break;
                    }
                    var next=current.Predicate==null?current.Next:((env[current.Predicate]!=0)==current.BranchOnTrue?current.Branch:current.Next);
                    previous=current;current=next;
                }
            }
            Require(checks>0,"oracle did not reach the use");return checks;
        }
    }
    static bool Static(object value)
    {
        string type=Get(value,"Type").ToString();
        if(type=="ConstantBuffer"||type=="Constant")return true;
        object op=Get(value,"AsgOp");
        return op!=null&&op.GetType().Name=="Operation"&&Get(op,"Inst").ToString()=="BitwiseOr"&&Static(Call(op,"GetSource",0))&&Static(Call(op,"GetSource",1));
    }
    static Graph Choose(string which)
    {
        var g=new Graph();var entry=g.Block();var yes=g.Block();var no=g.Block();var yesTail=g.Block();var noTail=g.Block();var merge=g.Block();
        object a=Cb(7,17),b=Cb(7,91);
        if(which=="same")b=Cb(7,17);
        if(which=="dynamic")a=g.Value(entry,"Load",Const(0),Const(0));
        if(which=="undef")a=New("Operand",E("OperandType","Undefined"));
        if(which=="commuted") { object x=Cb(4,55),y=Cb(9,123);a=g.Value(yes,"BitwiseOr",x,y);b=g.Value(no,"BitwiseOr",y,x); }
        entry.Link(no,yes,g.P);yes.Link(yesTail);no.Link(noTail);yesTail.Link(merge);noTail.Link(merge);
        // Def blocks deliberately differ from immediate incoming predecessors.
        g.Handle=g.Phi(merge,(yesTail,a,yes),(noTail,b,no));
        if(which=="same"||which=="finite"||which=="commuted")g.Use=merge;
        else {var use=g.Block();var exit=g.Block();merge.Link(exit,use,which=="different"||which=="undef"?g.Q:g.P);g.Use=use;}
        return g;
    }
    static Graph Nested(bool opposite=false)
    {
        var g=new Graph();Block gate=g.Block(),definition=gate;object value=Cb(7,13);
        for(int i=0;i<3;i++) {
            var write=g.Block();var merge=g.Block();
            gate.Link(write,merge,g.P);
            object next=i==0?Cb(7,17):i==1?g.Value(write,"BitwiseOr",value,Cb(3,88)):g.Value(write,"Copy",value);
            write.Link(merge);value=g.Phi(merge,(gate,value,definition),(write,next,write));gate=merge;definition=merge;
        }
        var use=g.Block();var exit=g.Block();gate.Link(opposite?exit:use,opposite?use:exit,g.P);g.Use=use;g.Handle=value;return g;
    }
    static Graph Budget(string which)
    {
        var g=new Graph();var entry=g.Block();g.Use=entry;g.Handle=Cb(1,5);
        if(which=="depth")for(int i=0;i<40;i++)g.Handle=g.Value(entry,"Copy",g.Handle);
        if(which=="cycle") {object v=Local();var op=Op("Copy",v,v);Call(entry.Native,"Append",op);g.Handle=v;}
        if(which=="edges") {Block b=entry;for(int i=0;i<140;i++){var next=g.Block();b.Link(next);b=next;}g.Use=b;}
        if(which=="loop") {var head=g.Block();var body=g.Block();var use=g.Block();entry.Link(head);head.Link(body,use,g.P);body.Link(head);g.Handle=g.Phi(head,(entry,Cb(1,5),entry),(body,Cb(1,5),body));g.Use=use;}
        if(which=="nodes") {
            var leaves=new List<Block>();
            void Tree(Block at,int depth) {if(depth==0){leaves.Add(at);return;}var yes=g.Block();var no=g.Block();at.Link(no,yes,Local());Tree(yes,depth-1);Tree(no,depth-1);}
            Tree(entry,8);var merge=g.Block();foreach(var leaf in leaves)leaf.Link(merge);
            g.Handle=g.Phi(merge,leaves.Select(b=>(b,Cb(6,27),b)).ToArray());g.Use=merge;
        }
        return g;
    }

    static Graph Composite(string which)
    {
        var g=new Graph();var entry=g.Block();
        object a=g.Value(entry,"CompareNotEqual",g.P,Const(0));
        object b=g.Value(entry,"CompareNotEqual",g.Q,Const(0));
        object rhs=g.Value(entry,"BitwiseNot",b), lhs=a;
        if(which=="bool_opposite")rhs=b;
        if(which=="bool_unknown")rhs=g.Value(entry,"CompareNotEqual",Cb(8,19),Const(0));
        if(which=="bool_integer_and") {
            lhs=g.Value(entry,"BitwiseOr",g.P,Const(1));
            rhs=g.Value(entry,"BitwiseOr",g.Q,Const(2)); a=lhs; b=rhs;
        }
        if(which=="bool_integer_not") {lhs=g.P;rhs=Const(-1);a=g.P;}
        object c=g.Value(entry,"BitwiseAnd",lhs,rhs);
        if(which=="bool_integer_not")c=g.Value(entry,"BitwiseNot",g.P);
        if(which=="bool_depth")for(int i=0;i<40;i++)c=g.Value(entry,"BitwiseAnd",c,Const(-1));
        if(which=="bool_nodes")for(int i=0;i<9;i++)c=g.Value(entry,"BitwiseAnd",c,c);
        if(which=="bool_cycle") {c=Local();Call(entry.Native,"Append",Op("BitwiseNot",c,c));}
        if(which=="bool_atoms") {
            // All nine atoms are structurally canonical. Even a known-false
            // first term cannot bypass the independent whole-expression bound.
            c=a;
            for(int i=0;i<8;i++)c=g.Value(entry,"BitwiseAnd",c,g.Value(entry,"CompareNotEqual",Cb(8,20+i),Const(0)));
        }
        var yes=g.Block();var no=g.Block();var merge=g.Block();
        entry.Link(no,yes,c);yes.Link(merge);no.Link(merge);
        object good=Cb(2,101),bad=New("Operand",E("OperandType","Undefined"));
        if(which=="bool_opposite"){var swap=good;good=bad;bad=swap;}
        if(which=="bool_finite")bad=Cb(2,103);
        if(which=="bool_dynamic")good=g.Value(yes,"Load",Const(0),Const(0));
        g.Handle=g.Phi(merge,(yes,good,yes),(no,bad,no));
        var gate=g.Block();var use=g.Block();var exit=g.Block();
        merge.Link(exit,gate,a);
        if(which=="bool_feasible_undef"||which=="bool_finite")gate.Link(use);
        else if(which=="bool_integer_and")gate.Link(exit,use,b);
        else gate.Link(use,exit,b);
        g.Use=use;return g;
    }

    static string Expression(object v)
    {
        string type=Get(v,"Type").ToString();
        if(type=="ConstantBuffer")return "CB:"+Call(v,"GetCbufSlot")+":"+Call(v,"GetCbufOffset");
        if(type=="Constant")return "CONST:"+Get(v,"Value");
        object op=Get(v,"AsgOp");
        if(op!=null&&op.GetType().Name=="Operation"&&Get(op,"Inst").ToString()=="BitwiseOr") {
            string[] x={Expression(Call(op,"GetSource",0)),Expression(Call(op,"GetSource",1))};Array.Sort(x,StringComparer.Ordinal);return "OR("+string.Join(",",x)+")";
        }
        return type;
    }
    static void DumpContract(string path)
    {
        using var doc=System.Text.Json.JsonDocument.Parse(System.IO.File.ReadAllText(path));var root=doc.RootElement;
        var blocks=new Dictionary<int,object>();var operands=new Dictionary<int,object>();
        foreach(var b in root.GetProperty("cfg").EnumerateArray())blocks[b.GetProperty("index").GetInt32()]=New("BasicBlock",b.GetProperty("index").GetInt32());
        foreach(var v in root.GetProperty("operands").EnumerateArray()) {
            string type=v.GetProperty("type").GetString();
            operands[v.GetProperty("id").GetInt32()]=type=="ConstantBuffer"?Cb(v.GetProperty("cbuf_slot").GetInt32(),v.GetProperty("cbuf_word").GetInt32()):
                type=="Constant"?Const(v.GetProperty("value").GetInt32()):type=="Undefined"?New("Operand",E("OperandType","Undefined")):Local();
        }
        foreach(var v in root.GetProperty("operands").EnumerateArray()) {
            object dest=operands[v.GetProperty("id").GetInt32()];string kind=v.GetProperty("kind").GetString();
            if(kind=="operation")Op(v.GetProperty("instruction").GetString(),dest,v.GetProperty("sources").EnumerateArray().Select(x=>operands[x.GetInt32()]).ToArray());
            if(kind=="phi") {
                object phi=New("PhiNode",dest);
                foreach(var src in v.GetProperty("sources").EnumerateArray())Call(phi,"AddSource",blocks[src.GetProperty("definition").GetInt32()],operands[src.GetProperty("value").GetInt32()]);
                Call(blocks[v.GetProperty("owner").GetInt32()],"Append",phi);
            }
        }
        foreach(var b in root.GetProperty("cfg").EnumerateArray()) {
            object block=blocks[b.GetProperty("index").GetInt32()];
            foreach(string prop in new[]{"next","branch"})if(b.GetProperty(prop).ValueKind==System.Text.Json.JsonValueKind.Number&&blocks.TryGetValue(b.GetProperty(prop).GetInt32(),out object target))Set(block,prop=="next"?"Next":"Branch",target);
            var d=b.GetProperty("dominator");if(d.ValueKind==System.Text.Json.JsonValueKind.Number&&blocks.TryGetValue(d.GetInt32(),out object dom))Set(block,"ImmediateDominator",dom);
            string term=b.GetProperty("terminator").GetString();
            if(term=="BranchIfTrue"||term=="BranchIfFalse")Call(block,"Append",Op(term,null,operands[b.GetProperty("condition").GetInt32()]));
        }
        var method=A.GetType("Ryujinx.Graphics.Shader.Translation.Optimizations.HandleUseProof",true).GetMethod("TryResolve",BindingFlags.Public|BindingFlags.Static);
        object[] call={operands[root.GetProperty("root").GetInt32()],blocks[root.GetProperty("use").GetInt32()],null};
        Require((bool)method.Invoke(null,call),"original IR dump still refused");
        Require(Static(call[2]),"original IR dump did not become a static expression");
        Console.WriteLine("case=dump expression="+Expression(call[2]));
    }

    static void BindingContract()
    {
        object handle=Cb(7,17),x=Local(),y=Local(),lod=Local(),d0=Local(),d1=Local();
        Array sources=Array.CreateInstance(T("Operand"),4),dests=Array.CreateInstance(T("Operand"),2);
        for(int i=0;i<4;i++)sources.SetValue(new[]{handle,x,y,lod}[i],i);
        dests.SetValue(d0,0);dests.SetValue(d1,1);
        object type=E("SamplerType","TextureBuffer"),flags=E("TextureFlags","Bindless, IntCoords");
        object tex=New("TextureOperation",E("Instruction","TextureSample"),type,E("TextureFormat","Unknown"),flags,0,0,3,dests,sources);
        Call(tex,"SetBinding",New("SetBindingPair",0,99));
        Require((int)Get(tex,"SourcesCount")==3,"binding failed to remove exactly the handle");
        Require(Call(tex,"GetSource",0)==x&&Call(tex,"GetSource",1)==y&&Call(tex,"GetSource",2)==lod,"binding changed coordinate/lod operands");
        Require(Call(tex,"GetDest",0)==d0&&Call(tex,"GetDest",1)==d1,"binding changed result operands");
        Require(Get(tex,"Type").Equals(type)&&Get(tex,"Flags").Equals(E("TextureFlags","IntCoords"))&&(int)Get(tex,"Index")==3,"binding changed texture type/flags/components");
        Console.WriteLine("case=binding coordinates_results_type_lod_preserved=True");
    }
    class FakeGpu : Ryujinx.Graphics.Shader.IGpuAccessor
    {
        int binding=1;
        public ReadOnlySpan<ulong> GetCode(ulong address,int minimumSize)=>ReadOnlySpan<ulong>.Empty;
        public Ryujinx.Graphics.Shader.SetBindingPair CreateConstantBufferBinding(int index)=>new(0,binding++);
        public Ryujinx.Graphics.Shader.SetBindingPair CreateImageBinding(int count,bool buffer)=>CreateTextureBinding(count,buffer);
        public Ryujinx.Graphics.Shader.SetBindingPair CreateStorageBufferBinding(int index)=>new(0,binding++);
        public Ryujinx.Graphics.Shader.SetBindingPair CreateTextureBinding(int count,bool buffer){int b=binding;binding+=count;return new(0,b);}
        public int QuerySamplerArrayLengthFromPool()=>32;
        public int QueryTextureArrayLengthFromBuffer(int slot)=>32;
        public int QueryTextureArrayLengthFromPool()=>32;
    }
    static void FallbackContract(string which)
    {
        var graph=Choose(which=="finite_fallback"?"finite":"dynamic");graph.Dominators();
        object coordinate=Const(123),dest=Local();
        Array sources=Array.CreateInstance(T("Operand"),2),dests=Array.CreateInstance(T("Operand"),1);
        sources.SetValue(graph.Handle,0);sources.SetValue(coordinate,1);dests.SetValue(dest,0);
        object tex=New("TextureOperation",E("Instruction","TextureSample"),E("SamplerType","TextureBuffer"),E("TextureFormat","Unknown"),E("TextureFlags","Bindless, IntCoords"),0,0,1,dests,sources);
        Call(graph.Use.Native,"Append",tex);
        var gpu=new FakeGpu();object resources=New("ResourceManager",E("ShaderStage","Vertex"),gpu,null);
        var pass=A.GetType("Ryujinx.Graphics.Shader.Translation.Optimizations.BindlessElimination",true).GetMethod("RunPass",BindingFlags.Public|BindingFlags.Static);
        pass.Invoke(null,new[]{graph.Use.Native,resources,gpu});
        Require((bool)Call(Get(graph.Use.Native,"Operations"),"Contains",tex),"unproven access was deleted");
        Require((int)Get(tex,"SamplerSet")>=0,"unproven access became a scalar static binding");
        Require(Call(tex,"GetSource",2)==coordinate&&Call(tex,"GetDest",0)==dest,"fallback changed coordinates/results");
        Array descriptors=(Array)Call(resources,"GetTextureDescriptors",true);
        Require(descriptors.Length==2&&descriptors.Cast<object>().All(d=>(int)Get(d,"ArrayLength")==32),"fallback resources were replaced with static slots");
        Console.WriteLine("case="+which+" original_operation_retained=True dynamic_arrays_retained=True");
    }
    static int Main(string[] args)
    {
        A=Assembly.LoadFrom(args[0]);string which=args[1];
        if(which=="dump"){DumpContract(args[2]);return 0;}
        if(which=="binding"){BindingContract();return 0;}
        if(which=="finite_fallback"||which=="dynamic_fallback"){FallbackContract(which);return 0;}
        Graph g=which.StartsWith("bool_")?Composite(which):which=="nested"?Nested():which=="opposite"?Nested(true):new[]{"depth","cycle","edges","loop","nodes"}.Contains(which)?Budget(which):Choose(which);
        g.Dominators();
        object[] invocation={g.Handle,g.Use.Native,null};
        var method=A.GetType("Ryujinx.Graphics.Shader.Translation.Optimizations.HandleUseProof",true).GetMethod("TryResolve",BindingFlags.Public|BindingFlags.Static);
        bool projected=(bool)method.Invoke(null,invocation);object resolved=invocation[2];
        bool expected=new[]{"nested","opposite","same","commuted","dynamic","bool_positive","bool_opposite","bool_dynamic"}.Contains(which);
        Require(projected==expected,"unexpected projection result for "+which+": "+projected);
        bool isStatic=projected&&Static(resolved);
        Require(isStatic==(expected&&which!="dynamic"&&which!="bool_dynamic"),"runtime input was promoted to a static binding");
        if(which=="opposite")Require(Get(resolved,"Type").ToString()=="ConstantBuffer"&&(int)Call(resolved,"GetCbufOffset")==13,"opposite guard selected the wrong definition");
        int checks=projected?g.CheckOracle(resolved):0;
        if(which=="different"||which=="finite"||which=="bool_finite")Require(g.Execute(7).Distinct().Count()>1,"negative fixture lacks two reachable values");
        Console.WriteLine("case="+which+" projected="+projected+" static="+isStatic+" reachable_path_checks="+checks);
        return 0;
    }
}
'''


class DirectBindingProof(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not DOTNET.is_file() or not (FORK / 'bin/Ryujinx.Graphics.Shader.dll').is_file():
            raise unittest.SkipTest('prepare the private --direct-static-proof fork and local SDK first')
        cls.work = Path(os.environ.get('FFRS_DIRECT_FIXTURES', str(BASE / 'remaining12-v4/fixtures')))
        cls.work.mkdir(parents=True, exist_ok=True)
        (cls.work / 'Program.cs').write_text(FIXTURE)
        references = ''.join('<Reference Include="%s"><HintPath>%s</HintPath></Reference>' %
                             (name, FORK / 'bin' / (name + '.dll'))
                             for name in ('Ryujinx.Graphics.Shader', 'Ryujinx.Common', 'Spv.Generator'))
        project = '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework></PropertyGroup><ItemGroup>' + references + '</ItemGroup></Project>'
        (cls.work / 'Fixture.csproj').write_text(project)
        cls.env = dict(os.environ, DOTNET_ROOT=str(DOTNET.parent), DOTNET_CLI_TELEMETRY_OPTOUT='1')
        command = [str(DOTNET), 'build', str(cls.work / 'Fixture.csproj'), '-c', 'Release', '-o', str(cls.work / 'bin')]
        identity = {'command': command, 'cwd': str(ROOT), 'fixture_sha256': hashlib.sha256(FIXTURE.encode()).hexdigest(),
                    'tested_shader_dll_sha256': hashlib.sha256((FORK / 'bin/Ryujinx.Graphics.Shader.dll').read_bytes()).hexdigest()}
        (cls.work / 'identity.json').write_text(json.dumps(identity, indent=2))
        with (cls.work / 'build.log').open('w') as log:
            result = subprocess.run(command, env=cls.env, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        if result.returncode:
            raise AssertionError((cls.work / 'build.log').read_text())

    def check_case(self, name):
        command = [str(DOTNET), str(self.work / 'bin/Fixture.dll'), str(FORK / 'bin/Ryujinx.Graphics.Shader.dll'), name]
        result = subprocess.run(command, env=self.env, capture_output=True, text=True, timeout=10)
        (self.work / (name + '.log')).write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('case=' + name, result.stdout)

    def test_canonical_and_not_selects_the_only_reachable_slot(self): self.check_case('bool_positive')
    def test_opposite_composite_selects_the_other_slot(self): self.check_case('bool_opposite')
    def test_unknown_comparison_keeps_feasible_undef(self): self.check_case('bool_unknown')
    def test_composite_feasible_undef_refuses(self): self.check_case('bool_feasible_undef')
    def test_composite_two_feasible_slots_refuses(self): self.check_case('bool_finite')
    def test_composite_runtime_input_stays_dynamic(self): self.check_case('bool_dynamic')
    def test_noncanonical_integer_and_never_uses_boolean_truth(self): self.check_case('bool_integer_and')
    def test_noncanonical_integer_not_never_inverts_branch_truth(self): self.check_case('bool_integer_not')
    def test_boolean_expression_cycle_refuses(self): self.check_case('bool_cycle')
    def test_boolean_expression_depth_refuses(self): self.check_case('bool_depth')
    def test_boolean_atom_budget_refuses(self): self.check_case('bool_atoms')
    def test_boolean_node_budget_refuses_shared_dag_expansion(self): self.check_case('bool_nodes')

    def test_original_sixty_ir_graphs_match_480_assignment_oracle(self):
        import importlib.util
        study = BASE / 'remaining13-study'
        spec = importlib.util.spec_from_file_location('independent_cfg_oracle', study / 'classify_ir.py')
        oracle = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(oracle)
        paths = sorted((study / 'diagnostic12/ir-dumps').glob('*.json'))
        self.assertEqual(len(paths), 60)
        checks = []
        for path in paths:
            result = oracle.analyse(path)
            leaves = result['handle_expression'][1:]
            expected = 'OR(' + ','.join(sorted('CB:%s:%s' % (x[1], x[2]) for x in leaves)) + ')'
            command = [str(DOTNET), str(self.work / 'bin/Fixture.dll'), str(FORK / 'bin/Ryujinx.Graphics.Shader.dll'), 'dump', str(path)]
            run = subprocess.run(command, env=self.env, capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('case=dump expression=' + expected, run.stdout)
            checks.append({'dump': path.name, 'dump_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                           'assignments': result['all_boolean_assignments_checked'], 'reachable': result['reachable_assignments'],
                           'resolved_expression': expected, 'command': command, 'stdout': run.stdout})
        (self.work / 'original60-oracle.json').write_text(json.dumps(checks, indent=2))
        self.assertEqual(sum(x['assignments'] for x in checks), 480)

    def test_nested_predication_reaches_fixed_point(self): self.check_case('nested')
    def test_opposite_guard_selects_other_source(self): self.check_case('opposite')
    def test_different_predicate_refuses_unique_slot(self): self.check_case('different')
    def test_two_feasible_slots_remain_dynamic(self): self.check_case('finite')
    def test_equal_slots_with_distinct_definition_blocks(self): self.check_case('same')
    def test_commuted_or_preserves_handle_value(self): self.check_case('commuted')
    def test_runtime_input_never_becomes_static(self): self.check_case('dynamic')
    def test_feasible_undefined_input_refuses(self): self.check_case('undef')
    def test_copy_cycle_refuses(self): self.check_case('cycle')
    def test_phi_backedge_refuses_without_invariant_proof(self): self.check_case('loop')
    def test_recursion_budget_refuses(self): self.check_case('depth')
    def test_cfg_walk_budget_refuses(self): self.check_case('edges')
    def test_node_budget_refuses(self): self.check_case('nodes')
    def test_actual_set_binding_keeps_coordinates_lod_results_and_type(self): self.check_case('binding')
    def test_real_pass_keeps_finite_multislot_access(self): self.check_case('finite_fallback')
    def test_real_pass_keeps_runtime_input_access(self): self.check_case('dynamic_fallback')


if __name__ == '__main__':
    unittest.main(verbosity=2)
