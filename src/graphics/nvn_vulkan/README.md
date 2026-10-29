# NVN → Vulkan for FFRS

A port of the D2R project's NVN-to-Vulkan replay engine onto FINAL FANTASY RESONANCE's object
model, plus the adapter that drives it and the host symbols it links against.

The engine decodes an NVN command stream and issues the Vulkan work it describes. It knows
nothing about the NVN API: the FFRS runtime owns the NVN entry table and the guest memory, and
hands the engine command memory and objects.

---

## 1. What came from where

| This tree | From | Revision | What |
|---|---|---|---|
| `engine/` (20 sources, their headers, `third_party/bcdec/`) | `.local/refs/d2r-mobile/dm/gfx/vulkan/engine/` | `a8782caa28d7b49d1b341e34de488d3168acb86c` | The replay engine. `xbg_game.cpp` is the executor. |
| `nvn/nvn_obj.h` | `.local/refs/nxnative/dm/gfx/nvn/nvn_obj.h` | `7a029f66d92c951ae6a86e38bb34eeb0cb7527e3` | FFRS's object model and command record format: 49 opcodes, 33 payload structures. |
| `nvn/nvn_tex.h`, `nvn/nvn_tex.c` | `.local/refs/nxnative/dm/gfx/nvn/` | same | FFRS's texture storage layout (Tegra block-linear), sparse-texture aware. |
| `nvn/nvn.h` | `.local/refs/nxnative/dm/gfx/nvn/include/nvn.h` | same | The NVN API types the objects are built from. |
| `nvn/nvn_cover.h` | `.local/refs/nxnative/dm/gfx/nvn/nvn_cover.h` | same | The full-coverage proof the engine uses to skip an attachment load. |
| `nvn/nvn_copy_pitch.h` | `.local/refs/d2r-mobile/dm/gfx/nvn/nvn_copy_pitch.h` | `a8782ca` | The copy stride / extent rule. FFRS has no copy of it; the semantics are the same. |
| `tex/nvn_deswizzle.c`, `tex/nvn_deswizzle.h` | `.local/refs/nxnative/dm/gfx/tex/` | `7a029f6` | Tegra block-linear ↔ linear. |
| `xtrace.h` | `.local/refs/d2r-mobile/dm/gfx/xtrace.h` | `a8782ca` | The (empty off-HarmonyOS) timeline markers. |
| `nvn_vk_adapter.c`, `nvn_vk_adapter.h` | `.local/refs/d2r-mobile/dm/gfx/vulkan/nvn_vk.c` | `a8782ca` | The adapter that drives the engine. |
| `xbg_host_bridge.c`, `xbg_host_bridge.h` | written here | — | The symbols the engine expects from a host and does not define. |

`engine/SOURCE.json` carries the upstream checksums and, for the five files this port changed,
their new ones. `engine/third_party/bcdec/` keeps the upstream `LICENSE` and `provenance.json`;
bcdec is MIT (© 2022 Sergii Kudlai) and is vendored unmodified.

### The relative includes

The engine's non-local includes were `../../nvn/…`, `../../tex/…` and `../../xtrace.h`. The engine
now sits one level below the root of this tree, so they are `../nvn/…`, `../tex/…` and
`../xtrace.h`. Five files changed for that reason alone (`xbg_game.cpp`, `xbg_texture.c`,
`xbg_bc_policy.h`, and the two headers named in `SOURCE.json`).

---

## 2. The object model: FFRS's, not D2R's

The engine was written against D2R's `nvn_obj.h` (43 opcodes). FFRS's has 49, and the payload
structures carry semantics the engine needs. The differences that matter:

| | D2R | FFRS (what this port uses) |
|---|---|---|
| Opcodes | 0..39, `OP_COUNT_` = 40 | 0..48, `OP_COUNT_` = 49 |
| Texture bind | one form | `OP_BIND_TEX1` (one combined handle) and `OP_BIND_TEX` (separate handles), the latter with `defer` / `src` |
| Texture slots | 0..127 | 0..127 combined, 128..255 separate (`NVN_SEPTEX_SLOT0`) |
| Draw record | fixed size | variable: the indirect and multi-draw fields are optional |
| Draw payload | `{prim, first, count}` | adds `instances`, `base_instance`, `ind`, `mdi_cnt`, `mdi_i` |
| Dispatch payload | `{x, y, z, caller}` | adds `ind` (the indirect group counts) |
| Copy stride | `OP_COPY_STRIDE` + `OP_COPY_IMAGE_STRIDE` | one `OP_COPY_STRIDE`, the `image` field says which |
| Bind payload | `{stage, first, n, h[32]}` | adds `defer` and `src` (the deferred handles are read at replay) |

The FFRS header is C. Three of its inline readers do an implicit `void *` → typed-pointer
conversion that C++ rejects, so the casts are explicit in `nvn/nvn_obj.h` (`bind_rec`,
`draw_rec`, `draw_idx_rec`). The logic is unchanged.

`src/graphics/nvn_device/nvn_obj_ffrs.h` in this repository already holds FFRS's object layouts
(`ffrs_tex`, `ffrs_view`, `ffrs_prog`, `ffrs_samp_par`, `ffrs_pool`, `ffrs_dpool`). They agree
with `nvn/nvn_obj.h` byte for byte, so the runtime's copies and this engine's readers meet.

The runtime's own headers define macros with the names of ten of the enumerators in `nvn.h` (the
`NVN_MEMORY_POOL_FLAGS_*`). `nvn/nvn_obj.h` clears those names before it includes `nvn.h`, with
the same values, so that the two headers can be read in either order.

---

## 3. The opcode mapping

The engine's `execute()` switch in `engine/xbg_game.cpp` runs on FFRS's numbering. The D2R
opcode 39 is gone and FFRS's nine extra opcodes are in.

| Value | D2R | FFRS (this port) | What the port does |
|---:|---|---|---|
| 0..38 | the same | the same | unchanged |
| 39 | `OP_COPY_IMAGE_STRIDE` | `OP_CLEAR_BUF` | the D2R case is deleted; FFRS's case is added |
| 40 | — | `OP_DEPTH_CLAMP` | added |
| 41 | — | `OP_RENDER_COND` | added |
| 42 | — | `OP_FENCE_SYNC` | added |
| 43 | — | `OP_TF_BIND` | added |
| 44 | — | `OP_TF_STATE` | added |
| 45 | — | `OP_RAST_DISCARD` | added |
| 46 | — | `OP_VP_SWIZZLE` | added |
| 47 | — | `OP_BIND_TEX1` | added; it joins the existing bind path |
| 48 | — | `OP_PRIM_RESTART` | added |
| 49 | `OP_COUNT_` | `OP_COUNT_` | the end of the NVN space in both |

The engine's own private opcodes (the tessellation patch size, the sync wait, the indirect
dispatch, the indirect draw, the event wait, the depth bounds, the resolve, the typed clear, the
draw plan) were numbered `OP_COUNT_ + n`, which with FFRS's numbering would have landed on the
game's own opcodes. They are now `XBG_OP_EXTENSION_BASE + n` with
`XBG_OP_EXTENSION_BASE = 64` (`engine/xbg_game_internal.h`), so 49..63 stay free for NVN.

The `default:` case still calls `xbg_fatalf("NVN opcode %u … not implemented")` for an opcode
that is in neither set.

### The semantics taken from the Metal reference

`nvn_mtl.m` in the nxnative tree is the Metal implementation of the same 49 opcodes and is the
semantic oracle. Each new opcode below says where its meaning comes from.

**`OP_CLEAR_BUF` — `nvn_mtl.m:7713`**
`mtl_sub_fill(cb->addr, cb->size, cb->value)`: a buffer range filled with a 32-bit value, in
submission order. Here the range is guest memory, so the fill is a host write of the repeating
4-byte pattern; the snapshot of the range is invalidated first, so a later GPU read of it sees
the new bytes. (`nvn_mtl.m:7713`, the record's own definition at `hle_nvn.c:2219`.)

**`OP_DEPTH_CLAMP` — `nvn_mtl.m:5093` and `nvn_mtl.m:6915`**
`S.depth_clamp = on`, and the Metal uses it as the opposite of its depth clip mode
(`cl = !S.depth_clamp`). On Vulkan that is `VkPipelineRasterizationStateCreateInfo::depthClampEnable`.
The `depthClamp` feature has to be enabled on the device; the engine refuses the draw otherwise.

**`OP_RENDER_COND` — `nvn_mtl.m:7695`**
Modes 0 and 1 compare the two 64-bit counters at `addr` and `addr + 16` (draw if equal / if not
equal); modes 2 and 3 force the render on / off. The Metal has no predication, so it evaluates
the condition on the CPU at replay and drops the draws and clears inside a block that is false
(`nvn_mtl.m:7735`, `7752`, `7769`). This port does the same: Vulkan's `VK_EXT_conditional_rendering`
is not enabled by the engine, so the condition is evaluated on the host when the record runs and
`state.render_cond_skip` gates the draws and the colour and depth clears. The block is cleared at
the start of every submission (`nvn_mtl.m:649`). Difference: the Metal draws when a counter is
not known yet, while here the pending GPU results over the range are resolved first, so the
decision is made on the value the guest will see — at the cost of a wait when that result is
still in flight.

**`OP_FENCE_SYNC` — `nvn_mtl.m:7701`**
`s->fence = g_fence + 1; s->fenced = 1;`: the sync object in the guest's memory is fenced with
the value the submission that reaches this point commits with. The engine's guest fence counter
counts the submissions, so the submission being translated completes at `submitted + 1`, and that
is what is written into the object (with a release store on `fenced`).

**`OP_TF_BIND` — `nvn_mtl.m:5097`**
`if (b->index >= 0 && b->index < 4) S.tf[b->index] = *b;`: the transform feedback buffer
binding is kept. The engine has no transform feedback stage, so the capture itself is not done;
see "not ported" below.

**`OP_TF_STATE` — `nvn_mtl.m:5098`**
`what` is 0 begin, 1 end, 2 pause, 3 resume. Begin resets the write offsets, pause and resume
keep them. Only the begin / resume state is kept here; the write offsets are the capture's, and
there is no capture.

**`OP_RAST_DISCARD` — `nvn_mtl.m:5101`**
`S.rast_discard = on`: nothing is rasterized. On Vulkan that is
`VkPipelineRasterizationStateCreateInfo::rasterizerDiscardEnable`. Vulkan ignores the fill mode,
the culling and the depth bias when the rasterizer is discarded, so those are forced to their
canonical values to keep the pipeline valid. The engine's "the first draw covers the whole
attachment, so the load can be skipped" shortcut is switched off while the rasterizer is
discarded, because such a draw writes nothing.

**`OP_VP_SWIZZLE` — `nvn_mtl.m:5094` and `nvn_mtl.m:6900`**
Per viewport, the source of the clip x, y, z and w, XOR the identity (0, 2, 4, 6). The Metal
implements one of them: `NEGATIVE_Y`, which negates clip y and is the viewport flipped about its
centre. Vulkan has the same thing as a viewport of the opposite height, so the same test
(`sw == 0x100`) flips the viewport here. The other swizzles (axis exchanges, negated x / z / w)
are not a viewport change; the Metal refuses them and draws unswizzled (`nvn_mtl.m:6902`), and so
does this port, with one log line.

**`OP_BIND_TEX1` — `nvn_obj.h`'s `bind_rec`, and `nvn_mtl.m:5073`**
One combined handle (the texture's number in the low 20 bits, the sampler's above them) in one
record. `bind_rec()` turns it into the separate texture / sampler form and adds the numbering
bases of the descriptor pools the record was made with. The record joins the engine's existing
bind path, which now also reads the deferred handles: a `defer` bind's handles live in the guest's
memory and are read when the record runs (`nvn_mtl.m:3370`, `bh_fresh`), not when it was
recorded.

**`OP_PRIM_RESTART` — `nvn_obj.h`'s `p_prim_restart`**
`on` and `index`. Vulkan has `primitiveRestartEnable` but no programmable restart index: the index
is fixed at 0xffff / 0xffffffff. The enable is honoured; an index that differs is refused once,
with a log line, and the fixed index is used.

### The draw records

FFRS's draw record is variable: it stops before the indirect and the multi-draw fields when they
are not used (`nvn_obj.h`: "A draw record stops before `ind` unless it is an indirect draw or a
multi-draw"), so the engine reads it with `draw_rec()` / `draw_idx_rec()` instead of requiring a
fixed size.

* `instances` of 0 counts as 1 (`nvn_obj.h`'s `p_draw`, and `nvn_mtl.m:7026`: `dw->instances > 0 ?
  dw->instances : 1`).
* `base_instance` is passed to the draw.
* `ind` is an indirect draw. The argument record is `{count, instances, first, base instance}` for
  arrays and `{count, instances, first index, base vertex, base instance}` for indexed draws
  (`nvn_mtl.m:7009`), which is the layout `xbg_draw_indirect_plan()` already reads. NVN reads the
  arguments on the GPU; this engine has no indirect draw buffer for guest memory, so they are read
  on the CPU at execution time, after the pending GPU writebacks over that range are resolved.
* `mdi_cnt` is an indirect-count multi-draw: the draw happens only while `mdi_i` is below the
  count the game wrote at `mdi_cnt` (`nvn_obj.h`'s `p_draw`). The Metal masks the arguments on the
  GPU so the draws past the count become no-ops; here the count is read at translation time and the
  draws past it are skipped. A count that the GPU writes later is therefore not honoured.

---

## 4. The host symbols

The engine expects a few symbols that it does not define. They are in `xbg_host_bridge.c`, with
the header explaining each one.

| Symbol | What it is |
|---|---|
| `uint64_t nvn_new_serial(void)` | The global increasing sequence the host and the engine share to name the host textures and programs (`nvn_obj.h`: "New serial (> 0, globally increasing)"). The engine calls it when it registers a texture by value; the FFRS runtime calls it when it creates a texture object. |
| `int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp)` | The dirty-page tracker's "was this range written" query. The engine declares it weak and only uses it together with the rest of the tracker (`nvn_dirty_tracked`, `nvn_dirty_protect`, `nvn_dirty_now`), which this port does not have. The stub answers "never written". |
| `int nvn_pool_flags(const void *addr, size_t len)` | The memory pool flags. `NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS` lets the engine skip a storage writeback a later writeback already covers. There is no pool flag registry here, so the stub answers "no flags" and no writeback is skipped. |
| `void xbg_record_command(cmd_t *c, const void *record, uint32_t bytes)` | Writes one cloned command record into the command buffer's current memory segment. The engine calls it from `xbg_copy_commands()`. When the segment is full the guest is asked for more through its NVN command-memory callback; a record that does not fit after two rounds is a named failure (`xbg_fatalf`), not a write past the end. |
| `xbg_host_command_callback` / `xbg_host_bridge_set_command_callback()` | The host's hook for the "the command buffer is out of memory" event. The engine calls the weak `xbg_host_command_callback` first and falls back to the guest's NVN callback when the host installed nothing. |

**Not implemented on purpose: `xbg_nvn_get_proc_address`.** The engine's `xbg_game.h` declares it
because the D2R tree declared it in the same header, but the engine never defined it: the real
NVN entry table lived in that project's SDK-specific `hle_nvn.c`, which is not ported. The FFRS
runtime supplies the NVN entry points; `xbg_game.h` now says so.

**`xbg_set_ui_rect_fn`** is the D2R project's optional UI-shift hook. The engine already defines
it and it stays optional: a null function pointer means nothing shifts.

---

## 5. The adapter

`nvn_vk_adapter.c` is the port of `.local/refs/d2r-mobile/dm/gfx/vulkan/nvn_vk.c`. It implements
the legacy `mtl_*` ABI on top of the engine's `xbg_*` API: it registers the guest's memory pools,
builds the engine's device, submits command memory, and presents.

What changed against the D2R original:

* The D2R host's own headers are gone. Its `LOG` and `die()` are the engine's `xbg_logf` and
  `xbg_fatalf`; the screen's refresh rate is a callback in the configuration
  (`xbg_vk_config::display_refresh`) instead of `front_display_refresh()`; the resource counter
  struct is declared in `nvn_vk_adapter.h`; the present pacer is the formula in the file.
* The configuration and its entry point are `xbg_vk_config` and `xbg_vk_initialize`: the `dm`
  prefix named the D2R project, not the ABI.
* `xbg_record_command()` is not here. It belongs to the host contract, so it is in
  `xbg_host_bridge.c` next to the other host symbols.
* The entry points the reference's header declares and this port has no use for are present with a
  clear meaning: `mtl_sub_fill` (the `OP_CLEAR_BUF` fill), `mtl_set_tex_pool_n` (the engine reads
  the capacity out of the pool record), `mtl_tex_init`, `mtl_tex_cpu_written`,
  `mtl_tex_gpu_owned`, `mtl_pkg_edge_skips`, `mtl_present_test_stall`, `xv_test_counts` and
  `prog_key`. `prog_key` answers "no such stage": the reference's per-stage key is the sha256 of
  the control section *and* the code area, while the engine's key is the digest of the control
  section alone, so the reference's key cannot be produced here.

The FFRS runtime does not have to use the `mtl_*` names. It may drive the engine directly:

* `xbg_legacy_pool_add()` for each guest memory pool,
* `xbg_register_texture(pool, id, texture, view)` and `xbg_register_sampler(pool, id, sampler)`
  for the descriptor pools (the engine's `legacy` registration by number alone is also there),
* `xbg_submit(count, handles)` with the handles pointing at the recorded command memory, which
  has to be registered as a pool first,
* `xbg_present_texture(&window, &texture)` for the frame.

The runtime's own recorder (`src/graphics/nvn_device/nvn_command_buffer.c`) already writes
packages in this exact record format and numbers its opcodes `NVN_CB_OP_*` one for one with the
reference's (0..48, `NVN_CB_OP_COUNT` = 49), so a package's bytes can be handed to the engine as
they are.

**The address contract.** The engine dereferences the guest's buffer addresses as host pointers
(its copies and its texture uploads read them directly), so the guest's pools have to live at the
addresses the guest uses. The runtime's memory pool already gives the guest the host pointer of
its storage ("the guest and the runtime share the address space", `nvn_memory_pool.c:433`), so
the two agree; the pool's other address, the one it hands out for the guest's own bookkeeping, is
not the address of the bytes and must not be used for buffer data.

---

## 6. The build

`build_engine.sh` compiles the tree for Android ARM64 with the NDK and links it into a shared
object with `--no-undefined`.

```
$ sh src/graphics/nvn_vulkan/build_engine.sh
CC   engine/xbg_command_clone.c
...
CXX  engine/xbg_vulkan.cpp
LINK <repo>/.local/build/nvn_vulkan/libxbg.so
OK   <repo>/.local/build/nvn_vulkan/libxbg.so
$ echo $?
0
```

The exact commands the script runs, per file:

```
CC=$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android28-clang

$CC -std=c11   -fPIC -fno-strict-aliasing -O2 -Wall -fsigned-char \
    -I<nvn_vulkan> -I<nvn_vulkan>/nvn -I<nvn_vulkan>/tex -c <file>.c   -o <file>.o
$CC -std=c++17 -fPIC -fno-strict-aliasing -O2 -Wall \
    -I<nvn_vulkan> -I<nvn_vulkan>/nvn -I<nvn_vulkan>/tex -c <file>.cpp -o <file>.o

$CC -shared -Wl,--no-undefined -o libxbg.so <objects> -lc++_static -lc++abi -lvulkan
```

* NDK: `/Users/yuyan9923/Library/Android/sdk/ndk/26.1.10909125`
* Compiler: `.../toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android28-clang`
* `-fsigned-char` is on `xbg_texture.c` only (it reads the guest's texture bytes as signed
  chars; the default is unsigned on arm64). The script puts it on every C file, which is
  harmless.
* The objects and the shared object go to `<repo>/.local/build/nvn_vulkan/`, outside the git tree
  (the repository ignores `.local/`, `*.o` and `*.so`).

**Result of the run recorded here:** exit code 0, `libxbg.so` = 1,810,448 bytes (1.73 MB),
5 compiler warnings, all of them in `engine/xbg_game.cpp` and all of them present in the upstream
D2R file as well (an uninitialised `bytes` behind a `&&`, an unused `sync_submit`, an unused
`support_cache`, and the unused functions `wait_fence` and `gpu_poll_complete`). The port adds no
warning.

**What this proves and what it does not.** It proves that every source in this tree compiles with
these flags for ARM64 Android and that the shared object has no undefined symbol. It does not
prove that the engine draws anything: that needs a Vulkan device, the FFRS NVN ABI, the guest's
memory pools and the game's exported shaders (`.xgs` bundles). Nothing here has been run on the
RG477M or on any other device.

---

## 7. What is NOT ported, and why

* **The NVN ABI.** `xbg_nvn_get_proc_address` is declared by the engine and defined by nobody
  here. The FFRS runtime owns the entry table.
* **Transform feedback capture.** `OP_TF_BIND` and `OP_TF_STATE` are recorded and the state is
  kept, but the engine has no transform feedback stage, so a draw under rasterizer discard does
  not write the vertex shader's outputs into the feedback buffers. The reference does that capture
  on the CPU (`nvn_mtl.m:6672`, `tf_draw`). Here the draw runs the vertex stage (so its side
  effects are kept) and one log line says that the capture did not happen.
* **A programmable primitive restart index.** Vulkan's index is fixed at 0xffff / 0xffffffff.
  The enable is honoured; another index is refused once, with a log line.
* **Viewport swizzles other than `NEGATIVE_Y`.** As in the reference: refused once, drawn
  unswizzled.
* **`VK_EXT_conditional_rendering`.** The render condition is evaluated on the host instead.
  The effect on the picture is the same; the difference is that the condition cannot change
  between the encoding of a draw and its execution, and a counter whose GPU result is still in
  flight is waited for rather than treated as unknown.
* **The dirty-page tracker and the memory pool flags.** Stubs; see the table in section 4.
* **The c2 driver block layout.** `fill_driver()` publishes the texture and sampler tables at the
  D2R game's offsets (0x168 and 0x568). That layout is the *game's shader ABI*, not part of the
  NVN API: FFRS keeps the combined texture handles and the separate ones in two tables of their
  own (c2 0x20 and 0x168, `nvn_obj.h`'s `NVN_SEPTEX_SLOT0`). The separate slots 128..255 are
  accepted by the bind path and reach the shader through the descriptor set, but they are not
  published in that block. The FFRS `.xgs` exporter has to agree with `fill_driver()` on the
  table bases.
* **The D2R project's host glue** (`dm/host/dm.h`, `dm/host/front.h`, `dm/gfx/nvn/nvn_mtl.h`,
  `dm/gfx/nvn/nvn_pace.h`) and its Python test harness under `engine/tests/`.

---

## 8. The tree

```
src/graphics/nvn_vulkan/
  README.md                  this file
  build_engine.sh            the ARM64 build
  xbg_host_bridge.c/.h       the host symbols the engine expects
  nvn_vk_adapter.c/.h        the adapter that drives the engine
  xtrace.h                   the (empty off-HarmonyOS) timeline markers
  engine/                    the 20 ported sources, their headers, third_party/bcdec/
  nvn/                       FFRS's nvn_obj.h, nvn_tex.c/h, nvn.h, nvn_cover.h, nvn_copy_pitch.h
  tex/                       nvn_deswizzle.c/h
```

The engine's sources are unmodified except for the five files named in `engine/SOURCE.json`.
