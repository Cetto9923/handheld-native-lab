# T-SHADER research — turning this guest's NVN/Maxwell shaders into SPIR-V

Date: 2026-10-24 · Role: read-only research worker (Explore) · No implementation performed.
Repo state at start: branch `work/rg477m-native`, HEAD `0039f3b09f97ed5ad12eba89858a387623761cd2`.
Pre-existing dirty state owned by another writer (NOT touched): ` M src/graphics/nvn_device/nvn_device.c`,
`?? src/graphics/nvn_device/nvn_sampler_pool.c`, `?? src/graphics/nvn_device/nvn_sampler_pool.h`.
Scope: decide the translation path so the implementer does not have to guess. Read-only everywhere
except this report file.
Note: while this report was being written, another writer committed `25cbd1d` (NVN sampler and
texture pool families) on the same branch, so HEAD moved `0039f3b` -> `25cbd1d`; the dirty files
listed above were committed by them, not by this task. All claims below were re-checked against
`25cbd1d`.

## 0. Answer in one paragraph

The guest's shaders are **NVIDIA Maxwell (Tegra X1) machine code in NVN "shader package" form**,
cooked by UE 5.6.1 into the `SF_GLSL_SWITCH` shader libraries inside this game's IoStore container.
Vulkan needs SPIR-V. The cheapest viable path is **offline translation with the Ryujinx
Maxwell→SPIR-V translator, then a content-addressed SPIR-V cache consumed at run time by
`vkCreateShaderModule`** — no SPIRV-Cross and no Metal step, because the translator already emits
SPIR-V with `TargetApi.Vulkan`. The translator, its job format, the FFR answer table and even a
**prebuilt macOS binary of the translation tool** already exist locally in the vendored `nxnative`
clone at pinned revision `7a029f66d92c951ae6a86e38bb34eeb0cb7527e3` (MIT). What this repo lacks is
(a) the NVN `program` family in `src/graphics/nvn/` (the guest traps there today) and (b) a way to
read shader bytes out of the guest — that is the only new code the recommended path needs, plus a
cache directory.

## 1. What the guest is, from local evidence only

| Fact | Evidence |
|---|---|
| Engine UE **5.6.1**, shader format `SF_GLSL_SWITCH`, shader platform `SP_GLSL_SWITCH` | `evidence/assets/shader_inventory.json:2-3` |
| Two shader libraries: `FFRS/Content/ShaderArchive-FFRS-SF_GLSL_SWITCH-SP_GLSL_SWITCH.ushaderbytecode` and `...-Global-...` | `evidence/assets/shader_inventory.json:13-27`; `.local/romfs/Manifest_UFSFiles_Switch.txt:2320-2323` |
| Global shader cache + pipeline cache (`FFRS_SF_GLSL_SWITCH.stable.upipelinecache`) | `evidence/assets/shader_inventory.json:4-12`; `Manifest_UFSFiles_Switch.txt:2318,3943` |
| Guest resolves `nvnProgramSetShaders`, `nvnProgramSetShadersExt`, `nvnProgramSetSubroutineLinkage` | `evidence/nvn/main_nvn_ids.txt:315-317` |
| Their guest call sites: `0x142e414`, `0x142e42c`, `0x143094c` | `evidence/nvn/guest_nvn_code_references.tsv:315-317` |
| Our runtime has **no** NVN program family — only the vendored header mentions it | grep `program` in `src/graphics/nvn` matches only `include/nvn/nvn.h` |
| The project already recorded this gap as open | `docs/analysis/nvn_device_requirements.md:171-175` |
| The project's own framing of the task (E4) | `coordination/HANDOFF.md:59,75` |

`SF_GLSL_SWITCH` is UE's *shader-format identifier*, not "GLSL source": the archive holds compiled
NVN machine code (§3.3/§3.4). Do not assume GLSL text is available.

## 2. Candidate paths

### A. Ryujinx Maxwell→SPIR-V translator — RECOMMENDED

- **Converts:** NVN/Maxwell shader package (0x50 SPH + instructions) → SPIR-V.
- **Input:** the code area (SPH + instructions), the control section, the `c1` constant segment
  (0x100 bytes, needed to resolve `BRX` indirect-jump targets), and answers to the translator's
  queries (stage, compute thread group / shared memory, texture and attribute types).
- **Output:** SPIR-V, emitted with `TargetLanguage.Spirv, TargetApi.Vulkan` — **Vulkan-flavoured
  SPIR-V directly**; SPIRV-Cross and Metal are not needed here. Evidence:
  `.local/refs/nxnative/dm/gfx/tools/dmsh_xlate/Program.cs:160`.
- **Licence:** MIT — the vendored subtree's `LICENSE.txt` reads "MIT License / Copyright (c)
  Ryujinx Team and Contributors" (`.local/refs/nxnative/dm/third_party/ryujinx/LICENSE.txt:1-3`).
- **Upstream URL + revision:** the upstream GitHub repository is **gone** —
  `https://api.github.com/repos/Ryujinx/Ryujinx` returns **HTTP 404** (verified 2026-10-24), its
  tags endpoint 404s, and GitHub repo search for `Ryujinx.Graphics.Shader` returns
  `total_count: 0`. The successor org `https://github.com/Ryubing` exists but its 11 public repos
  contain **no** emulator/translator repository. Secondary reporting states the project's GitHub
  assets were removed after Nintendo contacted the lead developer. **Citable immutable pin =
  the vendored copy inside nxnative:** `https://github.com/seedds/nxnative` @
  `7a029f66d92c951ae6a86e38bb34eeb0cb7527e3` (MIT; GitHub API `license.spdx_id: MIT`, default
  branch `main`; licence read at `.local/refs/nxnative/LICENSE:1-3`), path
  `dm/third_party/ryujinx`, which self-describes as "Three projects from Ryujinx 1.3.2:
  `Ryujinx.Graphics.Shader`, `Ryujinx.Common` and `Spv.Generator`"
  (`dm/third_party/ryujinx/README.md:1-3`).
- **Fit to THIS guest: excellent.** The vendored translator was already used on this exact game by
  nxnative: FFR profile `dm/games/0100606026F12000/` with a per-shader answer table
  (`shader_facts.tsv`, 2,900+ rows) and a reflection table (`shader_map.tsv`). For FFR the answer
  table carries **only** compute thread-group/shared-memory facts — "This game has no PC version to
  pair with: only the thread group / shared memory of compute shaders …; other queries get the
  translator defaults" (`dm/games/0100606026F12000/shader_facts.tsv:1`). A **prebuilt macOS binary**
  of the translation tool is present: `.local/refs/nxnative/dm/gfx/tools/dmsh_xlate_bin/dmsh_xlate`
  (+ `dmsh_xlate.dll`, `Ryujinx.Common.dll`, `Humanizer.dll`, `MsgPack.dll`) → the translation step
  needs **zero new build work**.
- **Caveats:** (i) upstream GitHub is unavailable → the vendored tree is the only supported source;
  (ii) nxnative had to patch the translator three times for other games (non-constant texel offsets,
  bindless handles through phis, 1D texture fetches) — those patches are already in the vendored
  tree and are MIT (`dm/third_party/ryujinx/README.md:5-10`); (iii) FFR has 2,077 vertex shaders that
  pick their texture slot in `c2` at run time (`dm/games/0100606026F12000/game.tsv:55-58`, profile
  key `tex_dyn_slots`), so binding must be array/dynamic, not per-shader constant.

### B. Yuzu/Hades Maxwell→GLSL recompiler → glslang → SPIR-V — second choice

- **Converts:** NVN binary → GLSL text → (glslang) → SPIR-V. Input: the NVN shader data (this
  recompiler accepts shaders *with* the 0x30-byte NVN header before the SPH). Output: SPIR-V via
  an extra GLSL round trip.
- **Licence / upstream:** MPL-2.0 (weak copyleft) —
  `https://github.com/DCNick3/shader-compiler-rs` @ `master`, last push 2025-06-22, head `f9f0fbc`
  (GitHub API `license.spdx_id: MPL-2.0`); "a thin wrapper around the Hades - Yuzu Shader
  Recompiler", same licence. The same MPL-2.0 code is also usable as a CLI (`shader-compiler-cli`).
  The GLSL→SPIR-V step needs `https://github.com/KhronosGroup/glslang` @ `main`, whose licence is
  composite (3-clause BSD core, plus 2-clause BSD, MIT, Apache-2.0 and **GPL-3.0 with bison
  exception**; `https://raw.githubusercontent.com/KhronosGroup/glslang/main/LICENSE.txt`, read
  2026-10-24; GitHub reports `NOASSERTION`).
- **Fit to THIS guest: poor.** Built for the Hades shader set; needs glslang as a second
  dependency whose GPL component is a licensing complication for a shipped runtime; and it emits
  GLSL that must then be re-compiled for Vulkan. Keep it as a cross-check of path A on one shader.
- **Note:** glslang *alone* is not a candidate — this guest has no GLSL source at runtime.

### C. SPIRV-Cross — post-processing / reflection, not on the recommended path

- **Converts:** SPIR-V → MSL/GLSL/HLSL, and reflects SPIR-V. Input: SPIR-V. Output: target-language
  source; `--reflect` JSON.
- **Licence / upstream:** Apache-2.0 — `https://github.com/KhronosGroup/SPIRV-Cross`, default branch
  `main`, last push 2026-10-14 (GitHub API `license.spdx_id: Apache-2.0`). nxnative pins
  **1.4.357** in its dependency table (`docs/DEPENDENCIES.md` in the vendored tree). **Not verified
  by me:** the immutable SHA for that tag — `git/ref/tags/1.4.357` and
  `git/matching-refs/tags/1.4.357` both returned 404/empty, so re-confirm the tag before pinning it.
- **Fit:** we want SPIR-V as-is for Vulkan, so this is not in the critical path. It *is* the natural
  tool for building the descriptor/binding table (cf. `dm/gfx/tools/vk_shaders.py`, which renumbers
  `(set,binding)` decorations and records them in a TSV) instead of hand-rolling reflection.

### D. Rejected alternatives

- **Translate on the device** (ship .NET, or port the translator to C++/Rust): rejected on cost —
  it puts a ~100 MB .NET runtime and multi-second per-shader translation in the device's frame path.
  nxnative explicitly avoids it: "Shaders are never translated at run time: the original Maxwell
  shader binaries are translated offline … and looked up by key"
  (`.local/refs/nxnative/docs/agents/04-graphics.md:3`).
- **A "NVN→Vulkan layer" that interprets Maxwell at run time**: rejected — it is a Maxwell ISA
  interpreter, orders of magnitude more work than path A, and it violates the project rule in
  `AGENTS.md` ("运行时通用 CPU 指令解释器/JIT 不满足本项目的原生验收定义").

### Reference implementation of the same problem (context, not a translator)

`https://github.com/jhaizhou-ops/d2r-mobile` @ `a8782caa28d7b49d1b341e34de488d3168acb86c` (MIT;
GitHub API `license.spdx_id: MIT`, default branch `main`; licence read locally at
`.local/refs/d2r-mobile/LICENSE:1-3`; local clone HEAD verified equal to that revision). It records
the NVN-HLE + replay-backend architecture this project follows; its shader chain is the same
"Ryujinx → SPIR-V → spirv-cross → metallib" family (Metal, so not directly usable here).

## 3. The actual shader data path in THIS game

### 3.1 Where the bytes live

The romfs mirror holds the real containers (sizes from `stat` on this machine; same numbers in
`evidence/assets/romfs_inventory.json` and `docs/analysis/nvn_device_requirements.md:171-175`):

```
.local/romfs/FFRS/Content/Paks/FFRS-Switch.pak    2,958,593,334 B   (configs, fonts, sound — NO shaders)
.local/romfs/FFRS/Content/Paks/FFRS-Switch.ucas   5,976,848,928 B   (the asset stream)
.local/romfs/FFRS/Content/Paks/FFRS-Switch.utoc      12,382,627 B   (the chunk table)
.local/romfs/FFRS/Content/Paks/global.ucas             3,353,200 B
.local/romfs/FFRS/Content/Paks/global.utoc                  818 B
```

**Gotcha:** `FFRS-Switch.pak` is hidden from plain file-name searches by the project's ignore rules
but is present on disk — use an explicit include-ignored glob or `stat`.

### 3.2 IoStore container layout (established locally)

`evidence/assets/romfs_inventory.json:72-83` (parsed by `scripts/extract/romfs_stream.py:221-236`):

```
magic "-==--==--==--==-"  version 8  header_size 144  entry_count 63910
compressed_block_count 518224  compressed_block_entry_size 12  compression_method_count 1
compressed_block_size 65536  directory_index_size 3096083  partition_count 1
```

Independently confirmed by the guest-path probe
`.local/pak-read-path/20261024T0010Z-realread/probe.stdout:62-67`: "IoStore magic ok",
"chunk count u32@0x18 = 63910", "block count u32@0x1C = 518224" (consistent with `romfs_stream.py`
reading `entry_count` at +0x18 and `comp_blk_cnt` at +0x1C).

The shader-bearing chunk taxonomy is upstream knowledge, not local evidence: `EIoChunkType`
**8 = ShaderCodeLibrary** (the tables, one chunk per library) and **9 = ShaderCode** (one chunk per
shader group) — `.local/refs/nxnative/dm/gfx/tools/uepak.py:277`
(`TOC_MAGIC, IO_SHADER_LIBRARY, IO_SHADER_CODE = b'-==--==--==--==-', 8, 9`); library-chunk layout
at `uepak.py:338-343` (library chunk: `u32 version (=1)`, shader-map hashes 20 B each, shader hashes
20 B each, shader-group chunk IDs 12 B each, shader map entries `{u32 first index, u32 shader
count}`, shader entries `u64 {frequency bits 0-3, group bits 4-33, offset bits 34+}`, group entries
`{u32 first index, u32 shader count, u32 raw size, u32 stored size}`). For FFR: "5,919 groups of
74,244 shaders" (`uepak.py:343`) and "73,496 unique shaders (9,253 vertex, 61,305 pixel, 2,934
compute, 4 geometry)" (`docs/agents/04-graphics.md:24`).

### 3.3 The NVN shader package (what the guest hands NVN)

From `.local/refs/nxnative/dm/gfx/tools/shader_split.py:34-40` and `docs/agents/04-graphics.md:35`:

```
code area (graphics):  0x30-byte header (magic 0x12345678)
                      + 0x50-byte SPH
                      + instructions (at +0x80)
code area (compute):   0x30-byte header (magic 0x12345679) + zero padding, instructions at +0x100
control section:       magic 0x98761234
                      +0x20  = the control section's own length
                      +0x700 = c1 offset inside the code area
                      +0x704 = code area length
                      +0x748 = compute thread-group size (u32 x3)
                      +0x754 = compute shared memory bytes
SPH word 0 bits 10..13 = stage (1 vertex, 2 hull, 3 domain, 4 geometry, 5 pixel)
key = sha256(control[:min(0x880, control+0x20)] + code area)[:16]
```

The translator decodes the SPH itself
(`dm/third_party/ryujinx/src/Ryujinx.Graphics.Shader/Translation/ShaderHeader.cs:96-168`): word0 =
SphType(0-4) | Version(5-9) | Stage(10-13) | MrtEnable(14) | KillsPixels(15) | DoesGlobalStore(16) |
SassVersion(17-20) | GpPassthrough(24) | DoesLoadOrStore(26) | DoesFp64(27) | StreamOutMask(28-31);
word1 = ShaderLocalMemoryLowSize(0-23) | PerPatchAttributeCount(24-31); word2 = HighSize |
ThreadsPerInputPrimitive; word3 = CrsSize | OutputTopology; word4 = MaxOutputVertexCount |
StoreReqStart | StoreReqEnd; words 6..13 = 32 imap types; word18 = type2OmapTarget; word19 = type2Omap.
Graphics instructions are decoded at `address + 0x50` (`.../Translation/Translator.cs:12,44-49`).

A second, independent upstream confirms the layout: the `shader-compiler-rs` README states that
"unlike the Ryujinx ShaderTools, this tool accepts shaders with the 0x30-bytes NVN-specific header
before the SPH" (MPL-2.0, master `f9f0fbc`).

### 3.4 UE's wrapper around it (`LSLG`)

Unreal stores an `LSLG` record per shader, not the bare package
(`docs/agents/04-graphics.md:22`): `LSLG` + stage tag (`SV`/`SP`/`SC`) + header with the source name
(`/Engine/Private/…usf:MainPS`) + `u32` code length + code area + `u32` control length (0x880) +
control section + a 24/29-byte tail, at unaligned offsets.

### 3.5 Can existing local tooling read it today?

- **Container header: yes.** `scripts/extract/romfs_stream.py:221-236` parses the `.utoc` header —
  but only the header: there is **no** chunk-table reader, no chunk extractor, and **no Oodle
  decoder** in this repo (grep `oodle` hits only engine-string evidence in `scripts/t00_verify.py`
  and `tests/acceptance/tools/verify_engine_strings.py`).
- **Raw bytes: yes.** `.local/pak-read-path/20261024T0010Z-realread/probe.stdout` shows the
  guest-path HLE opening `FFRS-Switch.pak/.ucas/.utoc` and reading positioned ranges (23 checks,
  0 failures) — a usable way to pull raw container bytes without writing a parser.
- **Decoded shader bytes: no (yet).** Both layers (container blocks and each shader group) are
  Oodle-compressed for UE 5.6 (`uepak.py:9-11`), and this repo has no Oodle decoder.
- **The guest can do it for us.** UE itself mounts and reads the containers on device
  (`coordination/HANDOFF.md:790-815`: UE opens `FFRS-Switch.pak`, `.utoc`, `.ucas` itself) and the
  guest links OodleLZ. That is why the recommended path captures from the guest instead of parsing
  the container.

## 4. What the NVN API itself says (vendored header, quoted)

`src/graphics/nvn/include/nvn/nvn.h:144-151`:

```c
typedef struct {
    char reserved[0xC0];
} NVNprogram;

typedef struct {
    NVNbufferAddress data;
    const void* control;
} NVNshaderData;
```

`src/graphics/nvn/include/nvn/nvn.h:1195`:

```c
typedef NVNboolean (*PFNNVNPROGRAMSETSHADERSPROC)(NVNprogram*, int, const NVNshaderData*);
```

`src/graphics/nvn/include/nvn/nvn.h:1548` and `:1718-1722`:

```c
typedef void (*PFNNVNCOMMANDBUFFERBINDPROGRAMPROC)(NVNcommandBuffer*, const NVNprogram*, int);
typedef NVNboolean (*PFNNVNPROGRAMSETSUBROUTINELINKAGEPROC)(NVNprogram*, int, const NVNsubroutineLinkageMapPtr*);
typedef void (*PFNNVNCOMMANDBUFFERSETPROGRAMSUBROUTINESPROC)(NVNcommandBuffer*, NVNprogram*,
    NVNshaderStage, const int, const int, const int*);
```

Stage identifiers, `src/graphics/nvn/include/nvn/nvn.h:630-651`:

```c
typedef enum {
    NVN_SHADER_STAGE_VERTEX = 0x0,
    NVN_SHADER_STAGE_FRAGMENT = 0x1,
    NVN_SHADER_STAGE_GEOMETRY = 0x2,
    NVN_SHADER_STAGE_TESS_CONTROL = 0x3,
    NVN_SHADER_STAGE_TESS_EVALUATION = 0x4,
    NVN_SHADER_STAGE_COMPUTE = 0x5,
    NVN_SHADER_STAGE_LARGE = 0x7FFFFFFF
} NVNshaderStage;
```

Two more header facts that matter at run time:

- `nvn.h:604-617` — memory pool flags include `NVN_MEMORY_POOL_FLAGS_SHADER_CODE = 0x40`: the guest
  marks the pool that holds shader code, so the runtime can tell which pool to read from.
- `nvn.h:310` — `NVN_DEVICE_INFO_SHADER_CODE_MEMORY_POOL_PADDING_SIZE = 0x4E`: the device can be
  asked for the shader-code pool padding, i.e. the guest expects shader code in a padded pool region.

**Reading the bytes:** `NVNshaderData.data` is an `NVNbufferAddress` (GPU address) and `.control`
is a `const void*` (host pointer). Our memory-pool layer records the guest-allocated range as the
pool's CPU mapping (`src/graphics/nvn_device/nvn_memory_pool.h:8-16`), so the code area can be read
from guest memory on the host without a device readback — **if** `data` is a pool address (to be
confirmed in the first capture).

**Not in the header:** `nvnProgramSetShadersExt` does not appear anywhere in the vendored NVN
53.105 header, although the guest resolves and calls it (`evidence/nvn/main_nvn_ids.txt:316`, call
site `0x142e42c`). Its signature must be recovered from the guest's call site.

## 5. Recommendation — one path, concrete steps

**Path: offline Maxwell→SPIR-V with the vendored Ryujinx translator, consumed at run time from a
content-addressed SPIR-V cache.**

Why cheapest: translator, job format, FFR answer table and a prebuilt binary of the tool already
exist locally under a pinned MIT revision; the translator already emits SPIR-V targeting Vulkan, so
SPIRV-Cross/Metal/glslang are off the critical path; and translation happens on the Mac host, never
in the device's frame path.

**Step 1 — implement the NVN `program` family (the only new runtime code).**
Add `nvnProgramInitialize / nvnProgramSetShaders / nvnProgramSetShadersExt / nvnProgramFinalize`
to `src/graphics/nvn/` as host-side state keyed by the guest's `NVNprogram*` (same discipline as
the device/queue/window families). Today these names trap. At `nvnProgramSetShaders(program, stage,
shaderData[])`:
1. read `shaderData[stage]` → `.data` (address) and `.control` (pointer);
2. read the control bytes from guest memory; validate `control[0:4] == 0x98761234` (LE `34 12 76
   98`) and take `ctl_len = min(0x880, u32@control+0x20)`;
3. read the code area of `u32@control+0x704` bytes from the `SHADER_CODE`-flagged pool's CPU
   mapping; validate `code[0:4] == 0x12345678` (graphics) or `0x12345679` (compute);
4. compute `key = sha256(control[:ctl_len] || code_area)[:16]` (nxnative's convention,
   `shader_split.py:80-81`) — this key is the contract between capture and cache;
5. write the capture to `.local/shaders/capture/<key>.<stage>.bin` holding `code_area ||
   control[:ctl_len] || c1[0x100]` plus a small JSON header (stage, code_len,
   `c1_off = u32@control+0x700`, thread_group, shared, control_sha256, code_sha256).
If a magic check fails, report the exact bytes and stop — do not guess which pointer is which.

**Step 2 — translate one shader offline, on the Mac, with zero new code.**
`.local/refs/nxnative/dm/gfx/tools/dmsh_xlate_bin/dmsh_xlate <job.json> <outdir>`; the job format
is documented at `dm/gfx/tools/dmsh_xlate/Program.cs:8-12`:
`{"key", "stage": "vs|ps|cs", "code": <file with 0x50 SPH + instructions>, "c1": <hex>, ...}`.
Split the capture first: `code_file` = code area **minus** the 0x30 header (starting at the SPH);
`c1` = the 0x100 bytes at `code_area[c1_off … c1_off+0x100]`. Output: `<key>.spv` + `<key>.res.json`.
Confirm SPIR-V magic `0x07230203` and the entry-point stage. Only `local`/`shared` answers are
needed for FFR (`dm/games/0100606026F12000/shader_facts.tsv:1`); other queries take the defaults.

**Step 3 — runtime lookup.**
On `nvnProgramSetShaders`, compute the key and look it up in the cache index
(`.local/shaders/spirv/index.tsv` → `key, stage, spv path, sha256, bindings`). On a hit:
`vkCreateShaderModule`, cache the module in the `NVNprogram` state; later
`nvnCommandBufferBindProgram` builds/caches a `VkPipeline` against our Vulkan descriptor layout.
On a miss: write the capture, log `shader <key> not in the cache`, and fail honestly (the project
rule forbids fake success).

**Step 4 — scale up.**
Once one shader works, bulk-produce the cache (73,496 shaders for this game,
`docs/agents/04-graphics.md:24`). Two input sources: (a) keep capturing from the guest as it loads
materials (slow, but needs no container parser); or (b) extract the two shader libraries straight
from the IoStore container — that needs an Oodle decoder this repo lacks, but nxnative shows the
trick: run the **guest's own** `OodleLZ_Decompress` in the host tool mode (`uepak.py:114-117`,
`OodleHost`, "served before the SDK starts, so no boot"). Our runtime already runs the guest, so
this is feasible — but it is a second work item; do not start it before step 2 proves the chain.

**What would make this fail**
- `nvnProgramSetShadersExt` has an unknown signature (absent from the vendored header) and the
  guest calls it; if it is the one the guest actually uses, the argument layout must be read off the
  guest's call site at `0x142e42c` before the program family is correct.
- `NVNshaderData.control` may be a host pointer *or* an address in this guest, and `.data` may point
  at the 0x30 header or at the SPH. The magic checks in step 1 exist to settle this on the first
  capture — do not assume.
- Binding model: FFR's 2,077 vertex shaders choose their texture slot at run time from `c2`
  (`game.tsv:55-58`), so a static per-shader descriptor layout will not bind correctly; the
  translator's bindless-to-array handling (`Ryujinx.Graphics.Shader/Translation/Optimizations/
  BindlessToArray.cs`) and a runtime-bound texture array are required.
- Depth/clip-space convention: the translator emits Vulkan SPIR-V, but the guest's NVN depth
  convention (NVN default near=-w vs near=0) and viewport Y direction must match Switch behaviour;
  nxnative handles this explicitly for Metal (`dmsh_xlate/Program.cs:4-6`) — the Vulkan equivalent
  must be checked, not assumed.
- Coverage: 4 geometry shaders and 2,934 compute shaders exist; geometry stages were not supported
  by nxnative's Metal route, and Vulkan geometry-stage support on Mali-G615 is not established —
  expect the first frame to need only vertex/fragment.
- Volume: translating ~73k shaders is an offline hours-scale job with an unknown SPIR-V cache size
  (the decoded UE archives alone are ~707 MB per upstream notes). Measure cache size and build
  time before this becomes the shipping plan.
- Licence hygiene: vendoring `dm/third_party/ryujinx` (MIT) into this repo is fine; **do not**
  vendor the MPL-2.0 recompiler (path B) or glslang (GPL-3.0-with-bison-exception component) into
  the shipped runtime.

## 6. Unknown / not established (do not guess)

1. **`nvnProgramSetShadersExt` signature and semantics** — absent from the vendored NVN 53.105
   header; the guest resolves it (`main_nvn_ids.txt:316`) and calls it at `0x142e42c`. Argument
   layout and difference from `nvnProgramSetShaders` are **not established**.
2. **Whether `NVNshaderData.control` is a host pointer or an NVN address in this guest, and whether
   `.data` points at the 0x30 header or at the 0x50 SPH.** The header types (`nvn.h:148-151`) do
   not say what the guest passes. **Not established.**
3. **Whether the guest's cooked binaries match nxnative's FFR build.** Our local build is a
   different title and version (`0100B8A026F14000`, v22.1.0 HK — `scripts/extract/materialize_paks.py:
   28-31`) than nxnative's FFR profile (`0100606026F12000`, update 1.0.1 / v65536, `game.tsv:7,17`).
   Whether the shader set, the `c2` handle layout and nxnative's `shader_facts.tsv` keys apply to
   **our** build is **not established**.
4. **The immutable SHA for SPIRV-Cross tag `1.4.357`** — the GitHub ref endpoints returned 404/empty;
   only the tag name from nxnative's dependency table is known.
5. **The upstream home/revision of the Ryujinx shader translator itself.** `Ryujinx/Ryujinx` is 404
   and no public repo named `Ryujinx.Graphics.Shader` exists; the only citable immutable source found
   is the vendored copy inside nxnative @ `7a029f66…`.
6. **The UE `LSLG` tail (24/29 bytes) and the `u32` code/control length semantics inside the IoStore
   library chunk** for *our* build — taken from upstream code, not verified against our container.
7. **Whether our container's shader chunks are byte-identical to nxnative's** — not verified; the
   container has not been decoded here (no Oodle decoder in this repo).
8. **Mali-G615 (RG477M) Vulkan limits vs the translated SPIR-V**: descriptor counts, subgroup size,
   and whether any FFR shader needs features the device lacks — **not established**.
9. **Whether the guest's NVN program usage needs subroutines** (`nvnProgramSetSubroutineLinkage`,
   `nvnCommandBufferSetProgramSubroutines` are resolved by the guest) — how they map to Vulkan is
   **not established**.
10. **What `nvnProgramGetSubtileSize` means for us** (guest resolves it, `main_nvn_ids.txt:311`) —
    no Vulkan analogue identified.

## 7. Evidence index

Local, this repo: `src/graphics/nvn/include/nvn/nvn.h:144-151,310,604-617,630-651,1195,1548,1718-1722`;
`src/graphics/nvn_device/nvn_device.h` (backend interface); `src/graphics/nvn_device/nvn_memory_pool.h:8-16`;
`evidence/nvn/main_nvn_ids.txt:311-317`; `evidence/nvn/guest_nvn_code_references.tsv:315-317`;
`evidence/assets/shader_inventory.json`; `evidence/assets/romfs_inventory.json:53-96`;
`scripts/extract/romfs_stream.py:221-236`; `scripts/extract/materialize_paks.py:26-44`;
`.local/romfs/Manifest_UFSFiles_Switch.txt:2318-2323,3943`; `.local/romfs/UECommandLine.txt`;
`.local/romfs/FFRS/Content/Paks/*` (sizes by `stat`); `.local/pak-read-path/20261024T0010Z-realread/probe.stdout`;
`docs/analysis/nvn_device_requirements.md:171-175`; `docs/analysis/nvn_graphics_chain_references.md`;
`coordination/HANDOFF.md:59,75,790-815`.

Local, vendored upstream clones (read-only): `.local/refs/nxnative` @ `7a029f66…` (MIT) —
`dm/third_party/ryujinx/{README.md:1-13,LICENSE.txt:1-3}`,
`dm/third_party/ryujinx/src/Ryujinx.Graphics.Shader/Translation/{ShaderHeader.cs:96-168,Translator.cs:12,24-68}`,
`dm/gfx/tools/{dmsh_xlate/Program.cs:1-12,26-27,44-51,151-160,shader_split.py:34-40,53-95,uepak.py:7-14,114-117,277-343,vk_shaders.py:1-24}`,
`dm/games/0100606026F12000/{game.tsv:1-65,shader_facts.tsv:1-25}`, `docs/agents/04-graphics.md:3,22,24,33-35`;
`.local/refs/d2r-mobile` @ `a8782caa…` (MIT), `LICENSE:1-3`.

Upstream (licence/revision verified 2026-10-24): `https://github.com/seedds/nxnative` @
`7a029f66d92c951ae6a86e38bb34eeb0cb7527e3` (MIT) · `https://github.com/jhaizhou-ops/d2r-mobile` @
`a8782caa28d7b49d1b341e34de488d3168acb86c` (MIT) · `https://github.com/DCNick3/shader-compiler-rs`
@ `master` `f9f0fbc` (MPL-2.0) · `https://github.com/KhronosGroup/SPIRV-Cross` @ `main` (Apache-2.0;
tag 1.4.357 named, SHA unverified) · `https://github.com/KhronosGroup/glslang` @ `main`
(composite BSD/MIT/Apache-2.0 + GPL-3.0 w/ bison exception) · `https://github.com/Ryubing` (org
listing only; no emulator/translator repo) · `https://api.github.com/repos/Ryujinx/Ryujinx` →
**HTTP 404** (upstream repository removed).

Not consulted / out of scope: no device commands were run, no game content was downloaded, `agy` was
not used, and by this task no repository file other than this report was created or modified.
Final `git status --short` on `work/rg477m-native` @ `25cbd1d`:
  ?? coordination/reports/T-SHADER-research-2026-10-24.md   <- this report (mine)
  ?? scripts/runtime/nvn_call_sites.py                       <- NOT mine: another writer's
                                                               read-only NVN call-site tool,
                                                               created in the same minute
