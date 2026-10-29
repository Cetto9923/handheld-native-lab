# FINAL FANTASY RESONANCE — the `.xgs` shader bundles

The Vulkan engine loads a shader as a **XGS1 bundle** (`.xgs`) from a runtime
directory. This file says how those bundles for this game are produced, what
the name of a bundle is, and what is and is not proved.

The producer is `scripts/graphics/build_ffrs_xgs.py`. It does the whole chain
in one run: extract the shaders out of the game's own IoStore container,
translate them from Maxwell machine code to SPIR-V, fix the module for the
target device, reflect it, and pack it.

---

## 1. The chain

| step | what happens | where |
|---|---|---|
| 1. extract | the control section and the code area of every shader, out of `FFRS-Switch.utoc` → two shader libraries → 5,919 groups (Oodle Mermaid compressed) → UE `LSLG` records | `.local/refs/nxnative/dm/gfx/tools/uepak.py` (the reference host's own container reader) + the local Oodle decoder |
| 2. translate | Maxwell → SPIR-V | `.local/refs/nxnative/dm/gfx/tools/dmsh_xlate_bin/dmsh_xlate` (the vendored Ryujinx translator, a .NET 10 application) |
| 3. reflect | the module + the translator's answer table → the `.vk.json` schema the packer reads | `build_ffrs_xgs.py`, `reflect()` |
| 4. pack | `.vk.json` + `.spv` → the XGS1 bundle | `build_ffrs_xgs.py`, `pack()`; the format is the one the reference host defines (`dm/gfx/vulkan/engine/xbg_game.h`, `xbg_shader_header` / `xbg_shader_binding`) |

The bundle layout, little endian:

```
xbg_shader_header  (32 bytes)   magic 0x31534758 "XGS1", version 1, stage, counts, sizes
xbg_shader_binding (32 bytes each)  binding, type, count, bytes, source, slot, sampler_slot, flags
uint32 input_locations[input_count]     the vertex stage's input locations
uint8  c1[c1_bytes]                      the frozen c1 constant segment
uint8  spirv[spirv_bytes]                the SPIR-V
```

---

## 2. The name of a bundle — `K_control`

**A bundle is named by the control key:**

```
K_control = sha256(control[:0x880])[:16]        (first sixteen hex digits)
```

This is what the engine computes from the control section the guest hands NVN
and what it looks up on disk: the reference host's
`dm/gfx/vulkan/engine/xbg_shader_key.c` computes exactly this — a hand-written
SHA-256 over the same 0x880 bytes with the standard padding — and the
`xbg_game_config` field is documented as `<control-sha256-first16>.xgs`.

The rule was checked against the engine's own code, not taken on faith. The
reference host's `xbg_shader_key.c` was compiled and run over a real control
section taken out of this game's container:

```
$ ./keytest .local/shader-identity/174293a30c315aed.control.bin
c_key=dbc58f224deb3525
$ python3 -c "import hashlib;print(hashlib.sha256(open('...control.bin','rb').read()[:0x880]).hexdigest()[:16])"
dbc58f224deb3525
```

**The two names must not be mixed.** This game has two:

| name | what it hashes | what it is |
|---|---|---|
| `K_control` | `sha256(control[:0x880])[:16]` | **the bundle's file name** — what the engine looks up |
| `K_content` | `sha256(control[:nctl] ‖ code[:ncode])[:16]` | the game's own name for a shader; the reference host's `shader_map.tsv` carries it, and the runtime cache of this port keys on it |

`nctl` is the control section's own length when it states one below 0x880
(`u32@control+0x20`), and 0x880 otherwise; `ncode` is the code area's length
(`u32@control+0x704`). Over the whole shader set of this game the two names are
one to one: 74,244 records, 73,496 distinct shaders, **0 control-key
collisions** (measured by `scripts/graphics/nvn_shader_identity.py`, and the
same count was reached again by this script's own index).

The script's internal identity is `K_content`, so the manifest can be compared
with the reference host's map line by line; the **file it writes is named by
`K_control`**.

---

## 3. The environment

| what | where | note |
|---|---|---|
| the game's data | `<data-root>/romfs/FFRS/Content/Paks/FFRS-Switch.{utoc,ucas,pak}` | `--data-root`, or `FFRS_DATA_ROOT`, or `<repo>/.local` |
| the container reader | `<data-root>/refs/nxnative/dm/gfx/tools/uepak.py` | the reference host's own code, imported read only |
| the translator | `<data-root>/refs/nxnative/dm/gfx/tools/dmsh_xlate_bin/dmsh_xlate` | a .NET 10 application |
| the .NET runtime | `<data-root>/shader-verify/dotnet` | there is no system `dotnet`; the script sets `DOTNET_ROOT` for the translator |
| the Oodle decoder | `<data-root>/shader-verify/oodle_dec` | the open source `ooz` implementation, built for x86_64, so it runs under Rosetta 2 |
| the answer table | `<data-root>/refs/nxnative/dm/games/0100606026F12000/shader_facts.tsv` | this game's own table: the compute thread group and shared memory, nothing else |
| `spirv-opt` | `/opt/homebrew/bin/spirv-opt` | SPIRV-Tools; present on this machine |
| `spirv-val` | `/opt/homebrew/bin/spirv-val` | SPIRV-Tools; present on this machine |

`spirv-opt` and `spirv-val` are found on the `PATH`; `SPIRV_OPT` and
`SPIRV_VAL` override them. Without `spirv-opt` the script computes the
capability set itself (the same rule, see §6); without `spirv-val` the modules
are not validated (`--no-validate` does that on purpose).

The work directory (the extracted code files, the translator's jobs, the
SPIR-V) is a temporary directory; `WORK` puts it somewhere else. It is removed
at the end unless `--keep-work` is given.

---

## 4. Running it

One shader (by content key or by control key — both are accepted):

```bash
python3 scripts/graphics/build_ffrs_xgs.py \
    --keys 174293a30c315aed dbc58f224deb3525 \
    --out .local/shaders/ffrs
```

Everything:

```bash
python3 scripts/graphics/build_ffrs_xgs.py --all --out .local/shaders/ffrs
```

A first `--keys` run walks the whole container (5,919 groups, about two
minutes) and writes an index (`<out>/index.tsv`) that says where every shader
is; later `--keys` runs read only the groups that hold the wanted shaders and
take seconds. A `--all` run rewrites the index.

Useful switches:

| switch | what it does |
|---|---|
| `--limit N` | build only the first N shaders |
| `--jobs N` | how many groups are read and how many translator runs run at once (default 8) |
| `--batch N` | how many shaders one translator run carries (default 32) |
| `--slot-base nvn\|table` | the texture slot in a bundle: `nvn` (the default) is the NVN slot, `table` is the index in the driver's handle table — see §7 |
| `--manifest PATH` | where the manifest goes (default `<out>/manifest.json`) |
| `--no-validate` | skip `spirv-val` |
| `--keep-work` | keep the intermediate files |

The run prints what it built and what it failed and why, and writes
`manifest.json` with, per shader: the content key, the control key, the stage,
the byte count, the sha256, the capability list and the capability fixes.

**The output is reproducible**: the same inputs give byte-identical bundles.
The translator is deterministic, `spirv-opt --trim-capabilities` is
deterministic, and every edit of the module is a word-level edit with a fixed
order.

---

## 5. What the reflection says

The `.vk.json` (and through it the bundle's binding table) carries, per
descriptor, the NVN **source** and **slot**:

| source | what the descriptor is | where the slot comes from |
|---|---|---|
| 1 support_buffer | the translator's support buffer (1392 bytes) | — |
| 2 uniform | the game's own uniform buffer | the constant bank the translator reports: bank 3…16 → uniform slot *n*−3 |
| 3 storage | a storage buffer | the c0 address table slot |
| 4 c1 | the c1 constant segment | — |
| 5 driver0 | the driver's c0 | — |
| 6 driver2 | the driver's c2 (the handle tables) | — |
| 7 texture | a texture (combined image sampler, or a texel buffer for a buffer image) | the driver's c2 handle table |
| 8 image | a storage image | the driver's c2 image table |

The slot of a texture or a sampler is decoded from the handle the shader uses:
the handle carries a word index into the driver's c2 table, and the tables are
the ones the NVN driver has in every game:

```
c2 0x20  + 8 r   combined texture handles
c2 0x120 + 8 r   image handles (8 slots)
c2 0x168 + 8 r   separate texture handles   → NVN texture slot 128 + r
c2 0x568 + 8 r   separate samplers          → NVN sampler slot r
```

This game binds **separate** textures and samplers (the profile's `game.tsv`
says so, and every handle of every shader of it decodes into the 0x168 and
0x568 tables), so the texture slot of a bundle is `128 + r` and the sampler
slot is `r`. `--slot-base table` gives the bare table index `r` instead.

The c1 bytes are the segment the control section names
(`code[u32@control+0x700 : u32@control+0x704]`, 0, 256, 512 or 768 bytes over
this game's shaders). The translator reads them to resolve the indirect jump
tables of the Maxwell code; without them the control flow of the particle
shaders goes wrong.

---

## 6. The device fixes (ARM Mali-G615, RG477M)

The device has no `shaderFloat64` and offers no
`VK_EXT_fragment_shader_interlock`. The translator's modules declare both, and
the driver refuses a pipeline that declares them (measured on the device:
`VK_ERROR_INITIALIZATION_FAILED`, see
`coordination/reports/T-SHADER-device-validation-2026-10-25.md` and
`coordination/reports/T-SHADER-fragment-2026-10-25.md`).

The fixes, in the order they are applied to the module:

1. **Binding renumbering** — the engine has one descriptor set per pipeline and
   the stages' bindings would collide, so the original binding number becomes
   `original × 6 + stage` (vertex 0, hull 1, domain 2, geometry 3, pixel 4,
   compute 5). A word-level edit: only the Binding decoration's operand moves.
2. **Capability trimming** — the capabilities the module does not use are
   removed: `Float64`, `Int64`, `Int16`, `Float16`, the subgroup family
   (`GroupNonUniform*`), `DrawParameters`, and
   `FragmentShaderPixelInterlockEXT`. The rule is the module's own content
   (the types it declares, the instructions it holds, the builtins it
   decorates), not a list of names. `spirv-opt --trim-capabilities` does the
   first pass; the rest is set here, because on some modules of this game
   `spirv-opt` removes capabilities the module **does** use (`DrawParameters`,
   `StorageImageExtendedFormats`) and the module then does not validate.
3. **The pixel interlock's extension declaration** — `OpExtension
   "SPV_EXT_fragment_shader_interlock"` (or `EXT_fragment_shader_interlock`)
   goes with its capability. The driver looks at the declaration, not at the
   use: dropping the capability alone is not enough (measured, `W2` in the
   fragment report).
4. **The storage image format capabilities** — a write to a storage image of
   unknown format needs `StorageImageWriteWithoutFormat` (56), a read needs
   `StorageImageReadWithoutFormat` (55). The translator does not declare them
   and the module does not validate without them.
5. **Whatever the validator names** — the module is validated with
   `spirv-val --target-env vulkan1.1`; if the validator says the module needs a
   capability, that capability is added and the module is validated again.
   This is how `StorageImageExtendedFormats` (49) is added to the modules whose
   storage images carry an extended format.

The project's own tool `scripts/graphics/fix_spirv_capabilities.py` does the
analysis and the editing; it refuses to drop a capability the module uses.

**No other optimisation is done.** The engine runs its own quad-lane and
warp32 rewriters over the module and they pattern-match the translator's
original output, so the module keeps its shape: only the binding decorations,
the capability section and the extension declarations move.

---

## 7. What is NOT verified

* **No frame.** A bundle that is built is a build fact. That the RG477M draws
  with it, that the descriptors bind, that the pixels are right — none of that
  is shown here. The device work that exists (the capability findings) used
  hand-fixed copies of six modules, not these bundles.
* **The engine's slot convention is a decision, not a measurement.** The
  bundle format is the reference host's; the reference host's engine is not in
  this project. The texture slot is `128 + r` (the NVN slot, matching this
  project's own NVN layer, `nvn_command_buffer.c`'s `NVN_CB_SEPTEX_SLOT0 = 128`)
  and the sampler slot is `r`. `--slot-base table` gives the other reading.
  Whoever writes the Vulkan engine has to pick one and say which.
* **The engine's capability whitelist is not known here.** The reference host
  allows a fixed list and calls `fatal` on anything else. This game's modules
  keep `ImageBuffer`, `ImageGatherExtended`, `ImageQuery` and `SampledBuffer`
  (all declared by the translator), which the reference host allows. The
  engine this port is building may want a different list; the capability list
  of every module is in the manifest.
* **The answer table of this game is nearly empty.** `shader_facts.tsv` holds
  only the compute thread group and shared memory, so every texture type,
  image format, vertex attribute type and fragment output type falls back to
  the translator's defaults (2D, `R8G8B8A8Unorm`, float). That decides the
  descriptor types and the image formats of the bundles, and a mismatch with
  the real attachments shows up as a pipeline creation failure. The one
  hand-written row (`CopyStencilToLightingChannelsPS` writes an `R8Uint`
  target) is in the table.
* **The 2,077 dynamic-slot vertex shaders are not built.** Their texture slot
  is picked at run time (the profile's `tex_dyn_slots` key), the translator
  cannot trace it and falls back to an array of 32 textures and 32 samplers,
  and the XGS1 bundle format has no way to say that. The reference host's
  solution is to put slot indices into that shader's c2 tables and bind slot
  *k* at element *k*; the bundle format would have to grow a source kind for
  it. The manifest names every such shader.
* **The depth convention is not addressed.** The translator emits Vulkan
  SPIR-V and the guest's NVN depth convention (near = −w, the GL convention, or
  near = 0) is runtime state. The reference host solves it for Metal by
  rewriting the depth in the shader source and choosing the entry point at
  run time; the Vulkan equivalent is not part of this pipeline.
* **The geometry shaders** (4 of them) are built, but the Mali-G615 has no
  geometry stage; whether the driver accepts them at all is open.
* **The SPIR-V is validated, not executed.** `spirv-val --target-env vulkan1.1`
  is a static check. The driver's own acceptance of these exact bytes is not
  measured here.


## Descriptor table correction (Codex device debugging, 2026-10-25)

`decode_handle` table lengths are **entry counts**, not byte spans. The separate
texture table has 128 entries at c2+0x168; the ordinary combined table ends at
c2+0x120. Otherwise a sampler word at c2+0x570 is incorrectly accepted as texture
129, then exported as NVN slot257. A texture instruction reading c2+0x120 refers
to bound image0, as the original compute pair `cp_t_tcb_48` / `cp_i_cb2_48_`
shows; sampled and storage descriptors must share that image identity.

For previously produced bundles, `repair_xgs_handle_bindings.cpp` reads the
translator's exact OpName handle identities and OpDecorate binding IDs. It
rewrites only the XGS source/slot/sampler words, rejecting unknown identities.
The existing SPIR-V, constants, inputs and all other metadata stay identical.
It writes changed bundles to a separate output directory and leaves inputs
untouched. Build and use it only on private `.local` inputs/outputs:

```sh
clang++ -std=c++17 -O2 scripts/graphics/repair_xgs_handle_bindings.cpp -o .local/repair-xgs
.local/repair-xgs .local/original-xgs .local/repaired-xgs .local/repair-report.tsv
```

Before deployment, compare independently regenerated bundles, assert that all
non-descriptor bytes match, record old/new digests, and back up replacements.
This repair was checked against 49 independently translated and spirv-val
validated game bundles. The RG477M's complete set of 71,375 existing generated
bundles was subsequently read and hashed as the application UID. This validates
resource deployment, not shader execution, full original-library coverage, or
visual/gameplay acceptance. Some original source shaders were never generated
by the earlier pipeline (for example unsupported dynamic texture arrays).
