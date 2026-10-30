# T-SHADER verify — does one real FINAL FANTASY RESONANCE shader translate to valid SPIR-V offline?

Date: 2026-10-25 · Role: verification worker (branch child of the coordinator) · Scope: read-only
everywhere except this report file; all experiment artifacts live under `.local/shader-verify/`
(gitignored).
Repo state during the work: branch `work/rg477m-native`, HEAD `76aabd79fcca826fd7471428a42236e4951729c8`.
Pre-existing dirty state owned by **other writers** (NOT touched by this task):
` M scripts/runtime/build_guest_process_contract.sh`, ` M src/android/native/CMakeLists.txt`,
` M src/graphics/nvn_device/nvn_device.c`, ` M src/graphics/nvn_device/nvn_device.h`,
` M tests/native/guest_process_contract.c`, `?? scripts/runtime/make_nvn_blend_state_variants.py`,
`?? scripts/runtime/nvn_call_sites.py`, `?? scripts/runtime/selfproof_nvn_blend_state.sh`,
`?? src/graphics/nvn_device/nvn_blend_state.c`, `?? src/graphics/nvn_device/nvn_blend_state.h`,
`?? src/graphics/nvn_device/nvn_render_state.c`, `?? src/graphics/nvn_device/nvn_render_state.h`.
Host: macOS 27.0.0 arm64, Apple clang 21.0.0, Rosetta 2 present. No Vulkan, no `spirv-val`, no
`spirv-cross` on this host (irrelevant for offline translation; relevant for §6).
Nothing under `src/`, `tests/`, `scripts/`, `coordination/state.json`, `coordination/HANDOFF.md`,
`coordination/STATUS.md` or `coordination/prompts/` was modified. No `git add/commit/push/checkout/
reset/clean` was run. No device (adb) was touched. Nothing under `.local/romfs/`, `.local/refs/`
or `.local/logs/` was deleted or moved.

---

## 1. VERDICT

**YES.** One real FINAL FANTASY RESONANCE shader — NVIDIA Maxwell machine code in NVN shader-package
form, cooked by UE 5.6.1 into the `SF_GLSL_SWITCH` libraries of this game's IoStore container —
was extracted from the shipped game data and translated **offline** into valid SPIR-V today, with
**zero new project code**. Six real shaders (1 vertex, 3 pixel, 2 compute) were translated, one per
stage class, all with exit code 0.

Evidence at a glance:

| Check | Result |
|---|---|
| Translation runs | `translated: 1 ok, 0 failed` — 6/6 shaders, exit code 0 |
| SPIR-V magic | `03 02 23 07` (little-endian `0x07230203`) on **all 6** outputs |
| SPIR-V version | 1.3 on all 6 |
| Instruction stream | walks cleanly, ends **exactly** at the file end on all 6 |
| Entry points | `main` with execution model Vertex (1), Fragment (3), GLCompute (2) — matching each shader's stage |
| Independent cross-check | the extracted shader keys are **byte-identical** to nxnative's independently generated `shader_map.tsv` (74,244 shaders) for this exact title → the bytes are the same ones nxnative used |
| Full-translation caveat | the translator itself is **not** a validator; `spirv-val` was **NOT_RUN** (not installed). Validation here is structural (magic, header, instruction walk, entry point). Driver acceptance on RG477M is also **NOT_RUN**. |

So the recipe recommended by `T-SHADER-research-2026-10-24.md` ("offline Ryujinx Maxwell→SPIR-V,
content-addressed cache, no SPIRV-Cross/Metal needed") is **empirically confirmed** on real game
bytes. The offline translation step is no longer an unknown.

The single next blocker is **no longer in the translation path**; it moved to the runtime side:
`src/graphics/nvn/` still has **no NVN `program` family** (the guest traps there today), so there is
nothing yet that (a) captures the guest's shader bytes at `nvnProgramSetShaders` and (b) looks the
SPIR-V up in a cache. See §6 for that list, plus the two supply-chain items that the coordinator
must decide on.

---

## 2. The exact command sequence that worked

All commands run with **cwd = `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`**.

### 2.1 One-time prerequisites (local, nothing installed globally)

**(a) .NET 10 runtime** — the vendored translator is a framework-dependent .NET 10 apphost
(`dmsh_xlate.runtimeconfig.json`: `Microsoft.NETCore.App 10.0.0`); this host had **no** .NET runtime
(`/usr/local/share/dotnet` absent), and the apphost aborts with "You must install .NET to run this
application." Installed into the scratch dir, not globally:

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native
curl -sSL https://dot.net/v1/dotnet-install.sh -o .local/shader-verify/dotnet-install.sh
bash .local/shader-verify/dotnet-install.sh --runtime dotnet --channel 10.0 \
    --install-dir "$PWD/.local/shader-verify/dotnet" --no-path
# -> Installed version: 10.0.13  (.local/shader-verify/dotnet/shared/Microsoft.NETCore.App/10.0.13)
```

**(b) An Oodle Mermaid decoder** — the shader groups inside the IoStore container are Oodle
Mermaid-compressed (§3). No decoder existed on this host and none is vendored in nxnative (nxnative
runs the *game's own* decoder inside its Mac host, which needs the whole nxnative host build).
For this verification the open-source **ooz** implementation was built locally (scratch only, not
vendored into the project):

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/shader-verify
curl -sSL -o ooz.tar.gz https://codeload.github.com/uilman/ooz/tar.gz/refs/heads/master
mkdir -p oozsrc && tar xzf ooz.tar.gz -C oozsrc
# two local edits to oozsrc/ooz-master/stdafx.h (arm64 + non-MSVC):
#   1. guard  #include <xmmintrin.h>  with  #if defined(__x86_64__) || defined(__i386__)
#   2. add a portable memicmp() helper (MSVC intrinsic; used only by ooz's PoE-bundle CLI path)
cd oozsrc/ooz-master
OUT=/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/shader-verify
for f in kraken lzna bitknit compress compr_entropy compr_kraken compr_leviathan compr_mermaid \
         compr_match_finder compr_multiarray compr_tans stdafx; do
  if [ "$f" = "kraken" ]; then
    clang++ -arch x86_64 -O2 -std=c++11 -w -Dmain=ooz_cli_main_unused -c $f.cpp -o "$OUT/x86_$f.o"
  else
    clang++ -arch x86_64 -O2 -std=c++11 -w -c $f.cpp -o "$OUT/x86_$f.o"
  fi
done
clang++ -arch x86_64 -O2 -std=c++11 -w -c "$OUT/oodle_dec.cpp" -o "$OUT/x86_oodle_dec.o"
clang++ -arch x86_64 -O2 -o "$OUT/oodle_dec" "$OUT"/x86_*.o
```

`oodle_dec.cpp` is a 20-line driver around ooz's public entry point
`int Kraken_Decompress(const byte* src, size_t src_len, byte* dst, size_t dst_len)`
(`oozsrc/ooz-master/kraken.cpp:4138`); ooz selects the codec from the block header — decoder type 10 =
Mermaid (`kraken.cpp:4118-4120`, `Mermaid_DecodeQuantum`). The x86_64 build needs **Rosetta 2** because ooz's hot paths use
SSE intrinsics (`ReverseBitsArray2048`, `COPY_64_BYTES`); an arm64 build would need those three
spots ported to NEON or plain C — not done here.

### 2.2 The per-shader sequence (the "winning command")

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native
export DOTNET_ROOT="$PWD/.local/shader-verify/dotnet"

# 1) extract the shader bytes from the IoStore container and write the translator job
python3 .local/shader-verify/extract.py game:0:SP
#    -> key=174293a30c315aed tag=SP stage=ps code_area=0x3200 body=12752 c1_off=0x3100 c1_len=0x100

# 2) translate (offline, on the Mac)
.local/refs/nxnative/dm/gfx/tools/dmsh_xlate_bin/dmsh_xlate \
    .local/shader-verify/shaders/174293a30c315aed.job.json \
    .local/shader-verify/spv

# 3) check the result
python3 .local/shader-verify/spv_check.py .local/shader-verify/spv/174293a30c315aed.spv
```

The whole thing is also wrapped in one script (re-runnable, idempotent, same hashes):

```bash
bash .local/shader-verify/run_verify.sh
```

### 2.3 What the scratch scripts are (and are not)

`.local/shader-verify/extract.py` is a **thin driver over nxnative's own reference code**, not new
logic: it imports `uepak.py` from `.local/refs/nxnative/dm/gfx/tools/` (read-only) for the IoStore
TOC parse and the LSLG record parse, and it reproduces `dmsh_build.py`'s `job_for()` conventions:

* job `code` file = `code_area[0x30:]` for graphics (the 0x50 SPH + instructions; the 0x30-byte
  NVN header is stripped) and `code_area[0x100:]` for compute (0x30 header + zero padding; the
  padding is asserted to be all zero) — `dmsh_build.py:313-325`;
* job `c1` = `code_area[u32@control+0x700 : u32@control+0x704]` — `dmsh_build.py:315-316`;
* job `stage` = `vs|ps|cs`; `debug`/`reduced` = nxnative's defaults (`true`/`true`);
* answer table rows from `.local/refs/nxnative/dm/games/0100606026F12000/shader_facts.tsv` are
  merged into the job when the key has one (compute `local`/`shared` for this title).

**Not** implemented in my scratch script (nxnative's `dmsh_build.py` does it): the two-part
*VertexA/VertexB* vertex-shader merge (`dmsh_build.py:286-311`) and the transform-feedback layout.
Neither triggers for this title in a 25-group sample of the game library (§3.5), but a
full-library build must use `dmsh_build.py`, not this script.

---

## 3. Input provenance — where the shader bytes came from

### 3.1 Step 1 of the assignment: no pre-extracted payload exists locally

Searched `.local/refs/nxnative` and the whole repo for pre-extracted shader payloads:

* `.local/refs/nxnative` git tree: 1,348 tracked files, **no** `.bin` / `.spv` / `.spvtext` / `.hex`
  blobs; the two large blobs in history are `shader_map.tsv` (9.2 MB) and `shader_pairs.tsv`.
* `shader_facts.tsv` (2,935 rows) holds **only** query answers (`{"local":[8,8,1],"shared":8224}`) —
  no file paths, no bytes.
* `shader_map.tsv` column 5 points at **virtual IoStore paths**
  (`/ue_shaders/ShaderArchive-FFRS-SF_GLSL_SWITCH-SP_GLSL_SWITCH.ushaderbytecode#UE4`), not at local
  files; `pso_manifest.tsv` is empty ("provisional profile").
* `FFRS-Switch.pak` was listed (2,363 files): **no** `.ushaderbytecode`, no shader archive — the
  shaders are IoStore chunks, exactly as nxnative's `uepak.py:9-11` documents.

⇒ the bytes had to be read out of the container, which is what §3.2-§3.4 do.

### 3.2 The container (level 0)

| Item | Value |
|---|---|
| Container chunk table | `.local/romfs/FFRS/Content/Paks/FFRS-Switch.utoc` — 12,382,627 B, sha256 `ceb45c57976a367768aad94e20735fc8b249d9600cdaac147730f85c3b2aac12` |
| Container data | `.local/romfs/FFRS/Content/Paks/FFRS-Switch.ucas` — 5,976,848,928 B, sha256 `69586840383b838b9ee13946f7d8ef38fd47566fc110d14fae353d4a3b82bd8e` |
| TOC version / chunks / blocks / block size | 8 / 63,910 / 518,224 / 65,536 |
| Chunk type census | 1 (ExportBundleData) 46,594 · 2 (ExportBundleData/Data) 11,394 · 6 (1) · **8 (ShaderCodeLibrary) 2** · **9 (ShaderCode) 5,919** |
| Container compression | method list = `['Oodle']`, but **every block of both shader-chunk types is `stored`** (51 blocks for type 8, 6,992 for type 9 — measured over all of them) → the IoStore layer needs **no** Oodle for shader data |

The two shader libraries in this container: global = 3,542 shaders in 297 groups; game = 70,702
shaders in 5,622 groups (74,244 shaders total, matching nxnative's count).

### 3.3 The library chunk and the shader-group chunk (level 1 and 2)

For the shader translated below (the **game** library, `ShaderArchive-FFRS-SF_GLSL_SWITCH-SP_GLSL_SWITCH`):

| Item | Value |
|---|---|
| Library chunk id (12 B) | `3c7b966cb992023300000008` (type byte `08` = ShaderCodeLibrary) |
| Library chunk range in the container stream | offset 0, length 3,142,636 |
| Library chunk sha256 | `557ba1f42884cd7221bc93f367b852e4c6dc44125c58f18c839bfb97f4b65c3d` |
| Shader-group chunk id (12 B) | `3ad4cca5b235725121992809` (type byte `09` = ShaderCode) |
| Shader-group chunk range | offset 6,439,239,680, length 65,123 (container stream) |
| Group payload as stored | 65,123 B, sha256 `856e7c2512554d9fcac3d80c935a57e948b55377a09169cb65bb4bfa6e8b2f1b`, first bytes `8c 0a …` = **Oodle Mermaid** block header (0x8C 0x0A) |
| Group payload after Oodle decode | 238,890 B (= the group's `raw` size in the library table), sha256 `c7776784c9ddc37089ce71301f4004a6d558541ff46068c84130ff985af27865` |

So there is exactly **one** Oodle layer on this title (the per-group one), not two: the IoStore
container blocks that carry the shader chunks are `stored`.

### 3.4 The shader itself (level 3) — the bytes handed to the translator

The decoded group is a run of UE `LSLG` records (`LSLG` + stage tag + source name + u32 code
length + code area + u32 control length + control section + tail). The record chosen:

| Item | Value |
|---|---|
| LSLG stage tag | `SP` (pixel) |
| Content key | `174293a30c315aed` = `sha256(control[:0x880] ‖ code_area)[:16]` |
| Code area | 12,800 B (0x3200), magic `78563412` = `0x12345678` (graphics), sha256 `4b79b1f9bda120a0b1c27a6ec6ad02b8ba6d4e8aad4b3e4a6b8360e2e3be0c8a` |
| Control section | 2,176 B (0x880), magic `34127698` = `0x98761234`, sha256 `dbc58f224deb3525fd27f1e949042b09393570019f18a96bdec4ea345fb2dfa0` |
| c1 segment | `code_area[0x3100:0x3200]` = **256 B** (`u32@control+0x700` = 0x3100, `u32@control+0x704` = 0x3200 = code-area length) |
| **Translator input file** | `.local/shader-verify/shaders/174293a30c315aed.code.bin` = `code_area[0x30:]` (SPH + instructions) — **12,752 B**, sha256 `3448aafa60edf1f233a9c5b20ef288123e8cb082d2cd1f4a7a9d2ceb6eed8e46` |
| Translator job file | `.local/shader-verify/shaders/174293a30c315aed.job.json` (job `c1` = the 256-byte segment hex; `tex`/`img`/`attr`/`fout` empty; `debug`/`reduced` = true; no answer-table row for this key → the translator's defaults) |

**Independent cross-check.** The key computed from these bytes is **byte-identical** to the row
nxnative generated independently for this title:

```
shader_map.tsv:  ps_game.bin  0  pixel  174293a30c315aed  /ue_shaders/ShaderArchive-FFRS-SF_GLSL_SWITCH-SP_GLSL_SWITCH.ushaderbytecode#UE4
```

All six shaders translated below produced keys present in `shader_map.tsv`.

### 3.5 Coverage sample (honest limits of this verification)

* 6 shaders were translated: 1 vertex (`9cb65be4f7d0d498`, code area 0x200, global library),
  3 pixel (`174293a30c315aed` game, `3fefb8e379a30f64` game, `2ae58c692fb7fbb8` global), 2 compute
  (`41b20b5f09269ad8` game, `86affd63144799de` global).
* The compute shaders exercise the answer-table path: `41b20b5f09269ad8` has control
  `+0x748 = (64,1,1)`, `+0x754 = 340`; the table row gives `local=[64,1,1]`, `shared=352` (340
  rounded up to 16, per `shader_split.py`), and the job carried both.
* A 25-group sample of the game library decoded to 350 pixel + 63 vertex shaders; **all** vertex
  shaders had SPH `(type=1, stage=1)` — the plain single-part form — and no `control+0x748`
  VertexA/VertexB entry appeared, so nxnative's two-part merge is not needed for this title
  (sample only; not a full-library proof).
* 74,244 shaders exist in the two libraries; **6** were translated here. The recipe is identical
  for all of them, but full-library coverage is NOT_RUN.

---

## 4. Output artifacts

### 4.1 The primary artifact

| Item | Value |
|---|---|
| Path | `.local/shader-verify/spv/174293a30c315aed.spv` (clean rerun copy: `.local/shader-verify/out/174293a30c315aed.spv`, identical bytes) |
| Size | **146,216 B** |
| sha256 | `5998a79541c74ab06d57879146f6794676b999232f00ba25a6a14d2a7ce1e952` |
| First 4 bytes | `03 02 23 07` → **SPIR-V magic OK** (little-endian `0x07230203`) |
| First 16 bytes | `03 02 23 07 00 03 01 00 00 00 00 00 3c 20 00 00` → magic, version 1.3, generator 0, id bound 8,252, schema 0 |
| Structure | 8,817 instructions; the instruction stream ends **exactly** at the file end |
| Entry point | `OpEntryPoint` → execution model **Fragment**, name `main` |

### 4.2 All six translated shaders

| Key | Stage | .spv size | .spv sha256 | Entry point | .res.json sha256 |
|---|---|---|---|---|---|
| `174293a30c315aed` | ps | 146,216 | `5998a79541c74ab06d57879146f6794676b999232f00ba25a6a14d2a7ce1e952` | Fragment `main` | `057c2e2bfe97efb5cafe65fb6063800fa284145d4f870a44aaecb2d9659fdde2` |
| `3fefb8e379a30f64` | ps | 128,900 | `c090c434859fb603d17644d6729ad3c0559ff6812eea08b196ea4c12e08ce714` | Fragment `main` | `45257ba77b957ec8f5b845f908b211fb0d3794686344d8f85041137e6ba36895` |
| `2ae58c692fb7fbb8` | ps | 2,164 | `394eb9bb8bd2882eabfe4d342660685236599cbf48202d799320240154834048` | Fragment `main` | `4efcb15e54794f1c38fe02b1798a88bfccf0b0bb0fe7b3d5ffffe989a6bdac8d` |
| `9cb65be4f7d0d498` | vs | 3,584 | `93d5b6353de0e0579a6b1c1d1c2d6e772ad2c5f0d530b9957f2a6cd6bd94c55b` | Vertex `main` | `d5a341c3ba502ceaaf32b7678f97bff51cf114fcf94ce36e07215c1e8f6fa77e` |
| `41b20b5f09269ad8` | cs | 102,424 | `f4920b40f43f9f606963ac6366283c39b75c43e12a19a15d6590b9b01b8e3b71` | GLCompute `main` | `c6c52ae058d09274f7a0b132d900d1e634e8005cdd30cd7096df9b14df3b03b7` |
| `86affd63144799de` | cs | 26,940 | `ea55bc52fe2f2721c02e045dd0408619dad5d824c4d5a817ee2f6f704f3ec53c` | GLCompute `main` | `5ee1e0998bd7cbe8b9452a1630acb27a0bdacb8021a61984db4f0eba7d1157e2` |

Every one of the six: magic `03 02 23 07`, SPIR-V 1.3, instruction stream ends exactly at the file
end, one `OpEntryPoint` named `main`.

### 4.3 Output shape of the translator (answers the "one SPIR-V per stage?" question)

Per job entry the translator writes **two files** into the output directory
(`dmsh_xlate/Program.cs:182-195`):

* `<key>.spv` — the SPIR-V binary (one file per shader **per stage**; a vertex/pixel pair is two
  separate jobs, because NVN hands them to the driver separately);
* `<key>.res.json` — the reflection sidecar: `cbufs`, `sbufs`, `textures`, `images` (each with its
  Vulkan `binding` and the NVN `cbuf_slot`/`handle`), `fetch1d`, `uses_frag_coord`, `frag_out_map`,
  `geometry_stride`, and the translator's `notes`.

**No cross-compiled source is emitted.** The translator targets SPIR-V/Vulkan directly
(`TargetLanguage.Spirv, TargetApi.Vulkan`, `Program.cs:160`), so there is no MSL/GLSL/HLSL step; a
GLSL/MSL cross-compile would be a separate `spirv-cross` invocation (nxnative does that later, for
Metal — we do not need it for Vulkan).

Reflection for the primary pixel shader: 7 constant buffers (slots 1-7), 16 textures (bindings
1-16), 0 images, `uses_frag_coord: true`, `frag_out_map: 15794175`, `notes: []`.

---

## 5. The full `dmsh_xlate` stdout/stderr of the successful run, verbatim

Command (cwd = `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`):

```
.local/refs/nxnative/dm/gfx/tools/dmsh_xlate_bin/dmsh_xlate \
    .local/shader-verify/shaders/174293a30c315aed.job.json \
    .local/shader-verify/out
```

stdout (verbatim, complete):

```
translated: 1 ok, 0 failed
```

stderr (verbatim, complete):

```
```

exit code: `0`

Full transcript of the clean end-to-end rerun (`bash .local/shader-verify/run_verify.sh`, saved at
`.local/shader-verify/final_transcript.log`):

```
### 1. extract the shader (game library, group 0, pixel shader)
== game group 0 tag SP
  key=174293a30c315aed tag=SP stage=ps code_area=0x3200 body=12752 c1_off=0x3100 c1_len=0x100 facts=no map=/ue_shaders/ShaderArchive-FFRS-SF_GLSL_SWITCH-SP_GLSL_SWITCH.ushaderbytecode#UE4

### 2. translate (cwd = /Users/yuyan9923/GitHub/ff-resonance-rg477m-native)
--- stdout ---
translated: 1 ok, 0 failed
--- stderr ---
--- exit code: 0 ---

### 3. check the SPIR-V
174293a30c315aed.spv: 146216 B  magic=True  version=1.3  bound=8252  instrs=8817  ends_exactly=True  entry=[('Fragment', 'main')]  valid=True
    sha256=5998a79541c74ab06d57879146f6794676b999232f00ba25a6a14d2a7ce1e952

### 4. artifacts
total 304
drwxr-xr-x   6 yuyan9923  staff      192 Oct 25 00:19 .
drwxr-xr-x  75 yuyan9923  staff     2400 Oct 25 00:19 ..
-rw-r--r--   1 yuyan9923  staff     3623 Oct 25 00:19 174293a30c315aed.res.json
-rw-r--r--   1 yuyan9923  staff   146216 Oct 25 00:19 174293a30c315aed.spv
-rw-r--r--   1 yuyan9923  staff        0 Oct 25 00:19 stderr.txt
-rw-r--r--   1 yuyan9923  staff       27 Oct 25 00:19 stdout.txt
057c2e2bfe97efb5cafe65fb6063800fa284145d4f870a44aaecb2d9659fdde2  .local/shader-verify/out/174293a30c315aed.res.json
5998a79541c74ab06d57879146f6794676b999232f00ba25a6a14d2a7ce1e952  .local/shader-verify/out/174293a30c315aed.spv
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  .local/shader-verify/out/stderr.txt
655d7dd776eb03c580a10f87f60b407d302fc3e6f94095c0da80614b2ad13c9b  .local/shader-verify/out/stdout.txt
```

The first attempt of the same command, **before** the .NET runtime was installed locally, failed
as follows (kept here because the coordinator should know the tool has a runtime dependency):

```
You must install .NET to run this application.

App: /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/refs/nxnative/dm/gfx/tools/dmsh_xlate_bin/dmsh_xlate
Architecture: arm64
App host version: 10.0.13
.NET location: Not found

The following locations were searched:
  Application directory:
    /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/refs/nxnative/dm/gfx/tools/dmsh_xlate_bin/
  Environment variable:
    DOTNET_ROOT_ARM64 = <not set>
    DOTNET_ROOT = <not set>
  Registered location:
    /etc/dotnet/install_location_arm64 = <not set>
  Default location:
    /usr/local/share/dotnet
```

The six-shader run (one job file per shader, each in its own invocation) printed
`translated: 1 ok, 0 failed` six times, exit code 0 each time.

---

## 6. Remaining blockers for wiring this into the NVN `program` family

The translation path is proven; what is left is the runtime side. Ordered by how much they block
the first guest pixel.

1. **The NVN `program` family does not exist yet.** `src/graphics/nvn/` has no
   `nvnProgramInitialize / nvnProgramSetShaders / nvnProgramSetShadersExt / nvnProgramFinalize`
   (the guest traps there today). This is the only new runtime code the offline path needs.
   `nvnProgramSetShadersExt` does not exist in the vendored NVN 53.105 header at all, although the
   guest resolves and calls it (`evidence/nvn/main_nvn_ids.txt:316`, call site `0x142e42c`) — its
   signature has to be recovered from the guest call site.

2. **How the guest's shader bytes get captured at run time.** `nvnProgramSetShaders(program, stage,
   shaderData[])` hands over `NVNshaderData { NVNbufferAddress data; const void* control; }`:
   `control` is a host pointer (read the control section directly), `data` is a GPU address into the
   pool flagged `NVN_MEMORY_POOL_FLAGS_SHADER_CODE = 0x40` (`nvn.h:611`) — read the code area
   (`u32@control+0x704` bytes) from that pool's CPU mapping. Validate `control[0:4] == 0x98761234`
   and `code[0:4] == 0x12345678` (graphics) / `0x12345679` (compute) before using the bytes; stop
   with the exact bytes if a magic check fails rather than guessing which pointer is which.
   *This is the one part of the recipe that has not been exercised against live guest data here* —
   everything in §3 came from the shipped container, not from a running guest.

3. **How the SPIR-V is keyed for reuse.** The key is
   `sha256(control[:min(0x880, u32@control+0x20)] ‖ code_area)[:16]` — confirmed identical to
   nxnative's convention (`shader_split.py:74-81`) and to the 74,244 rows of
   `dm/games/0100606026F12000/shader_map.tsv`. The c1 segment is *inside* the code area, so it is
   covered by the key. Consequence: nxnative's existing map can drive the cache directly, and the
   runtime's key computation can be checked against it.

4. **What `c1` is and where it comes from.** `c1` is NVN's **c1 constant bank** (32 registers ×
   4 B = 256 B) that the NVN driver programs before the draw; in the NVN shader package it is the
   **last 0x100 bytes of the code area** (here `code_area[0x3100:0x3200]`, from
   `u32@control+0x700`). Its practical role for us: it holds the **jump tables** the translator
   reads through `LDC c[1]` to resolve indirect `BRX` targets — without it the translator cannot
   resolve control flow and particle shaders loop forever (nxnative's `Program.cs:44-45`). It must
   therefore be passed in the job (`"c1": <hex>`); the translator answers reads outside its length
   with 0 and notes it. Note that for the smallest shaders of this title `c1` is empty (code area
   0x100 bytes, c1 offset 0x100) — the translator copes with that (empty `c1`, no notes).

5. **The answer table is empty for this title's graphics shaders.**
   `shader_facts.tsv` for FFR carries **only** compute `local`/`shared` ("This game has no PC
   version to pair with"). Every texture type, image format, vertex attribute type and fragment
   output type therefore falls back to the translator's defaults (2D / `R8G8B8A8Unorm` / float),
   and the reflection shows `"format": "Unknown"`. On Vulkan these defaults decide the descriptor
   types and image formats, so a mismatch with the real attachments will show up as a pipeline
   creation failure or wrong sampling. Resolving this needs either a runtime-recorded answer table
   (nxnative's `DM_PSO_RECORD` / `pso_manifest.tsv`, still empty) or a per-shader capture.

6. **FFR binds textures dynamically.** 2,077 vertex shaders pick their texture slot from `c2` at
   run time (`game.tsv`, profile key `tex_dyn_slots`), so the Vulkan descriptor set must be an
   array/bindless layout, not per-shader constants.

7. **Supply chain for the offline build (decision needed from the coordinator).** The Oodle Mermaid
   decoder used here is the open-source **ooz** (`https://github.com/uilman/ooz`, snapshot of
   `master` as of 2020-09-12, tarball sha256 `1b7188b16bc233574eb05a33ef338d5d21a4b51bc9275932def597e121310589`).
   **That snapshot contains no LICENSE file** → the terms are unspecified; it is therefore used
   here only as a local, non-shipped analysis tool and is **not** vendored into the project. If the
   project wants a repeatable *build* pipeline, it must choose one of: (a) a properly licensed Oodle
   decoder, (b) the game's own decoder through nxnative's `DM_OODLE_SERVE` host mode (heavy: needs
   the nxnative Mac host), or (c) **commit the pre-built SPIR-V cache to the repo**, so no decoder
   is needed on the device or in CI. Option (c) matches nxnative's own model ("Shaders are never
   translated at run time … looked up by key").

8. **Toolchain.** `dmsh_xlate` needs a .NET 10 runtime on the build machine (installed locally
   here, §2.1). The device needs none of it: the SPIR-V cache is built once on the Mac and consumed
   on the RG477M through `vkCreateShaderModule`.

9. **Not verified here (NOT_RUN), for the record.** `spirv-val` / a Vulkan driver's acceptance of
   these modules (no `spirv-val` and no Vulkan on this host); `vkCreateShaderModule` on the
   RG477M; full-library translation of all 74,244 shaders; the two-part vertex-shader merge and the
   transform-feedback layout (not needed in a 25-group sample, see §3.5).

10. **Where this leaves the plan.** The research report's Step 2 ("translate one shader offline,
    with zero new code") is now **done and verified**. The next implementation step is Step 1 (the
    NVN `program` family + the capture at `nvnProgramSetShaders`) followed by Step 3 (the
    content-addressed SPIR-V cache lookup keyed as in §6.3). The capture path is the only piece
    that has never been run against live guest memory.

---

## Appendix — files created by this task

Only under `.local/shader-verify/` (gitignored) plus this report:

| Path | What |
|---|---|
| `.local/shader-verify/run_verify.sh` | the one-command end-to-end verification |
| `.local/shader-verify/extract.py` | IoStore TOC → group → LSLG → translator job (thin driver over nxnative's `uepak.py`) |
| `.local/shader-verify/spv_check.py` | structural SPIR-V check (magic / header / instruction walk / entry point) + sha256 |
| `.local/shader-verify/provenance.py`, `toc_probe.py`, `lib_probe.py`, `grp_probe.py`, `dump_group.py`, `parse_lslg.py`, `sample_vs.py`, `inspect_cs.py`, `pak_list.py`, `scan_cs.py` | the probes used to establish the container layout, the chunk taxonomy and the compute code-area layout |
| `.local/shader-verify/oodle_dec.cpp`, `oozsrc/`, `ooz.tar.gz`, `oodle_dec`, `*.o` | the local Oodle Mermaid decoder (see §6.7 for the licensing caveat) |
| `.local/shader-verify/dotnet/` | the local .NET 10.0.13 runtime |
| `.local/shader-verify/shaders/` | the extracted code files and job files of the six shaders |
| `.local/shader-verify/spv/`, `.local/shader-verify/out/` | the translated SPIR-V + reflection JSON |
| `.local/shader-verify/final_transcript.log` | the verbatim transcript quoted in §5 |

No file outside `.local/shader-verify/` and this report was created or modified.
