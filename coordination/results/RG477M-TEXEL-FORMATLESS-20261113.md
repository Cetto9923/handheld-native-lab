# RG477M — "Texel format lacks formatless-write support" (2026-11-13)

Task: RG477M-TEXEL-FORMATLESS-20261113.
Device: RG477M01445122 (Anbernic RG477M, Android 14, **Mali-G615 MC6**, driver 184553472).
Base: `task/T-release-1.0.9-20261112` (`dc3bbb9`), branch `task/T-rg477m-texelfix-20261113`.
The Honor tablet (`A42BBB5109000333`) was **never** touched; no `adb kill-server`, no
uninstall, no push.

## 1. Root cause

**The engine bound a 16-bit view (`R16_UINT`) to a formatless storage texel buffer
whose module writes four 32-bit words. SPIR-V's Image Format operand has no 16-bit
format, so the typed-storage fallback could not declare the bound format and the
module stayed formatless; the Mali-G615 reports no
`VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT` for that format, so the
descriptor guard aborted the process.**

Two distinct defects produce it, both inside the typed-storage path:

**(A) The bound format cannot be declared in SPIR-V.**
`texel_binding_format()` (`xbg_game.cpp`) resolves a formatless storage texel
buffer to the guest's native view format whenever the shader's scalar class
conflicts with it (the formatless early return). For
`3b6302a58b86a34a` binding 47 the guest's native format is **R16_UINT (74)** —
16 bits — while the module writes a `v4float`/`uint4`. SPIR-V's Image Format
operand has **no 8-bit and no 16-bit format at all**, and a `Dim = Buffer` image
may only carry `Rgba32f/i/ui`. `xbg_spirv_image_format(R16_UINT)` is therefore
`Unknown`, `typed_storage_variant()` skipped the binding
(`xbg_game.cpp:3161`, "SPIR-V cannot express that format"), the module stayed
formatless, and the descriptor path's formatless-write guard
(`xbg_game.cpp:4317`) called `xbg_fatalf`.

**(B) A shared image type was refused unconditionally.**
After (A) was fixed the game reached the next module,
`9bee7f09d6c655c3`: its bindings **29 and 35 share one** formatless storage
Buffer image type (`%31`). `xbg_typed_storage_rewrite()` refuses any image type
used by more than one descriptor, so the typed form was impossible and the same
fatal followed. A shared type declares one format for all of its users, so it may
only be declared when **every** user needs the same one — which is what the
engine now checks.

### The briefing's hypothesis is withdrawn

The briefing proposed that `89386a8` gated `declared` on `rewritten`, so a
module that is "already typed but not engine-rewritten" fell into the formatless
branch, or that `typed_storage_variant()` skipped the binding because
`declared != UNKNOWN`.

**Both are refuted by the evidence.** The module's image type is
`%42 = OpTypeImage %float Buffer 0 0 0 2 Unknown` — formatless before *and*
after the FFRS integer rewrite (which only changes the sampled type to `%uint`
and copies the Image Format operand unchanged). `declared` is therefore
legitimately `Unknown` for binding 47, and `typed_storage_variant()` did **not**
skip it: it computed the bound format and gave up at the "SPIR-V cannot express
that format" branch. The log ordering proves it — the format query for 74 appears
at guest_boot line 42533, **before** `STORAGE_PENDING_VIEW_MISS` (42534), which
is inside `descriptors_build()`, so the query came from
`typed_storage_variant()`, not from the descriptor path.

### Evidence chain

- `guest_boot-1.0.9.log:42527` — `NVN integer image types shader=3b6302a58b86a34a stage=5 loader=runtime rewrites=2`
- `guest_boot-1.0.9.log:42532` — `NVN shader 3b6302a58b86a34a stage=5 loaded words=22454`
- `guest_boot-1.0.9.log:42533` — `NVN texel format=74 bufferFeatures=0x58 formatFlags2=1`
  (74 = `VK_FORMAT_R16_UINT`; `0x58` = UNIFORM/STORAGE_TEXEL_BUFFER|VERTEX_BUFFER, **no**
  `STORAGE_WRITE_WITHOUT_FORMAT`)
- `guest_boot-1.0.9.log:42534` — `NVN STORAGE_PENDING_VIEW_MISS frame=22 shader=3b6302a58b86a34a`
  (this is the *storage buffer* path, not the texel buffer: adjacent noise)
- `guest_boot-1.0.9.log:42551-42552` — `FATAL: Texel format lacks formatless-write support`
- Module (extracted with `unzip -p` from `FFRS-data.zip`, entry
  `FFRS/romfs/shaders/3b6302a58b86a34a.xgs`, 22454 words, stage 5, 11 bindings):
  binding 47 = `type=5` (STORAGE_TEXEL_BUFFER), `source=8` (IMAGE), `slot=0`,
  `flags=0x80000501` (bit0 = formatless write, dim = 5 = Buffer);
  `%42 = OpTypeImage %float Buffer 0 0 0 2 Unknown`; two `OpImageWrite` of a `v4float`.
- The same binding shape in the sibling modules `51f7cdbbdeb581a3` and
  `664748e31a2dfad9` (`type=5`, `source=8`, `slot=0`, `flags=0x80000501`,
  `%20 = OpTypeImage %float Buffer 0 0 0 2 Unknown`, one `v4float` write) resolves
  to 32-bit formats (109, 100, 107, 98) and produces working typed variants. Only
  the module whose guest format is 16-bit fails.

## 2. Changes

| File | Change |
| --- | --- |
| `src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h` | `xbg_spirv_image_scalar_class()`, `xbg_spirv_storage_write_components()`, `xbg_typed_storage_view_format()`, `xbg_spirv_shared_image_bindings()`; `xbg_typed_storage_rewrite()` takes `shared_ok` |
| `src/graphics/nvn_vulkan/engine/xbg_game.cpp` | `texel_binding_format()` takes the module and substitutes a declarable view format when the device cannot do the formatless write; `typed_storage_variant()` allows a shared image type when all its users need the same format, and accepts the idempotent rewrite (0) |
| `tests/device/test_typed_storage.cpp` | the new contract rules and their red mutants |
| `tests/device/test_typed_storage_real_texel.cpp` | new: the two real modules from the game |
| `scripts/verify_host.py` | the new fixture output, `spirv-val` on the rewritten Buffer image, the two real-module steps |

**The rule.** For a formatless storage texel buffer whose bound format SPIR-V
cannot declare, and only on a device that cannot perform the formatless write,
the engine binds the format the module itself describes: the same numeric class,
32 bits per component, and the component count the module writes. A `Dim =
Buffer` image is always four components, so that is `R32G32B32A32_UINT` for
this module — which SPIR-V declares `Rgba32ui`, the only typed form a Buffer
image can carry. The view and the module then describe the same texels and the
existing strict match holds.

**The Adreno path is untouched.** The substitution only engages when the device
does **not** support the formatless write for the bound format — that is, in
exactly the case where the engine used to abort. On the Honor tablet the
formatless write for `R16_UINT` works (the tablet ran these modules and died of
a different fatal), so `storage_texel_formatless_write_ok()` is true there, the
substitution does not trigger, the module stays formatless and the bound format
stays the guest's — byte-identical behaviour to 1.0.9. The shared-image rule
likewise only engages when the device needs the typed form.

## 3. Tests

`python3 scripts/verify_host.py` — see §5 for the run.

- `typed-storage-contract` (`tests/device/test_typed_storage.cpp`) covers: the
  substitution for a bound view SPIR-V cannot declare, that a declarable bound
  view is left alone, that a module which declares its own format keeps it, the
  idempotency of the substitution on the rewritten module, the shared image type
  (refused by default, allowed when every user needs the same format), and the
  red mutants that already existed.
- `typed-storage-buffer-val`: `spirv-val --target-env vulkan1.1` on the
  rewritten module — `OpTypeImage %uint Buffer 0 0 0 2 Rgba32ui` is valid SPIR-V.
- `typed-storage-real-texel-contract` and `typed-storage-real-shared-contract`:
  the two real modules, extracted from the game package.

**Mutation proof** (the assertions are not tautologies):

| Mutation | Result |
| --- | --- |
| `xbg_typed_storage_view_format()` returns the guest's view (the old rule) | `typed-storage-contract` aborts at the substitution assertion (exit 134) |
| same mutation | `typed-storage-real-texel-contract` aborts (exit 134) |
| `xbg_typed_storage_rewrite()` without `shared_ok` | the shared-image assertion fails (exit 134) |

## 4. RG477M result

APK `ffrs-game-window-debug.apk`, sha256
`c210f06b84915e315b1009c63a3a52f2d36ca29bf273bd3e1194bc7ebc21ff67`, built by
`bash scripts/android/build_game_window_apk.sh` (exit 0), installed with
`adb -s RG477M01445122 install -r` (exit 0, **no uninstall**, save
`GameSaveData0/1` preserved, versionCode 9 unchanged). It is signed with the
project's local development key, the same key the installed 1.0.9 carries, so
the update installs over it.

Session: launch `2026-11-13T02:21:35+0800`, **pid 20632**, log `engine.log`
session = `.local/rg477m-texelfix-20261113/device/engine_session.log`
(6656 lines, `FATAL` count **0**), `engine.log.fatal` **absent**.

| Stage | Result | Evidence |
| --- | --- | --- |
| Title screen | **PASS** | `s3_title.png` — the game's own title with Continue / Load Game / New Game / Option |
| Continue → 3D scene | **PASS** | `s4_longpress.png` — a rendered 3D scene (character, fires, ruins, minimap, system menu) |
| Actual movement / interaction | **PASS** | `s5_move_before.png` → `s5_move_after1.png` → `s5_move_after2.png` — the left stick was pushed forward and right for 3 s each; the camera and the scene moved, the minimap marker moved; the process stayed up |
| No fatal, no abort | **PASS** | 0 `FATAL` in the session log; `engine.log.fatal` absent; pid alive throughout |

The engine now produces the typed variants that used to be refused:

```
NVN typed storage variant shader=3b6302a58b86a34a identity=3b6302a58b86a34a:stb1:47_107
NVN typed storage variant shader=9bee7f09d6c655c3 identity=9bee7f09d6c655c3:stb1:29_98,35_98
NVN typed storage variant shader=876dd0820a7ad8fa identity=876dd0820a7ad8fa:stb1:47_100,53_99
NVN typed storage variant shader=b673e10f3da2451c identity=b673e10f3da2451c:stb1:41_98,47_99
NVN typed storage variant shader=70a97cd190a8738f identity=70a97cd190a8738f:stb1:29_98,35_98
```

The last four are modules that no typed variant existed for before this fix: they
share one image type between two bindings.

## 5. Not covered, and the risks

1. **The frame rate is not an acceptance result.** The scene runs at 18–30 fps
   on the 477M; the target is 60. Nothing here claims otherwise.
2. **The substituted view changes the texel stride for this one binding**
   (2 bytes/texel → 16 bytes/texel) on the Mali only. The module writes four
   32-bit words per texel, so 16 bytes is what the module produces, but the
   guest reads the buffer back with its own declared format. **The guest's
   16-bit declaration for this pooled formatless buffer texture is the deeper
   question**; the fix makes the Mali path deliver what the shader wrote, and
   leaves the guest's format alone. This should be re-examined together with
   the tablet, where the same binding still runs formatless at 16 bits.
3. **The tablet was not re-run.** The change is inert on a device that supports
   the formatless write, but that was reasoned, not measured. An independent
   re-run of the Honor build is still owed.
4. **The shared-image rule only covers the case where all users agree.** If two
   bindings sharing one image type ever need different formats, the rewrite is
   still refused and the device would need the formatless feature. The general
   answer is to split the image type per descriptor; that was not implemented.
5. **Storage images (not texel buffers) keep the same exposure.** The
   substitution was implemented for the storage texel buffer path only, because
   the bound format there *is* the buffer view's format. A storage image's view
   format comes from the image view, so the same substitution would need the
   view changed with it. No storage image hit this in the observed sessions.
6. The battle scene itself was not played through. The stages above are the
   title, the 3D field and movement; combat, the dark-magic effect and the
   battle-settle crash of the Honor build are **NOT_RUN**.
