# RG477M — shared storage image type split (2026-11-13)

Task: RG477M-SHARED-TEXTURE-SPLIT-20261113.
Device line: **RG477M only** (Anbernic RG477M, Mali-G615 MC6). The Honor tablet
(`A42BBB5109000333`) was **never** touched: no adb command was issued to it, no
`adb kill-server`, no uninstall, no data clear, no push.
Branch: `task/T-rg477m-shared-texel-split-20261113`, base `4156929`.
Worktree: `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/shared-texel-split-20261113`.

## 1. Root cause

The battle compute module `ba0d8842fa53e521` (stage 5, 13 bindings) aborts the
RG477M because two of its descriptor bindings **share one formatless storage
image type** and need **different bound view formats**. A shared `OpTypeImage`
declares exactly one format for all of its users, so the typed-storage rewrite
could not declare either format and the module stayed formatless; the
Mali-G615 reports no `VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT`, so
the descriptor guard aborted the process.

### Device evidence (crash log of the RG477M run)

`/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/acceptance-1.0.9-honor-20261113/MOB/crash-20261113/guest_boot.log`

```
3017718: [engine] NVN shader ba0d8842fa53e521 stage=5 loaded words=19145 ms read=5.25 rewrite=2.10 analyse=1.72 module=0.11 reused=0
3017719: [engine] NVN shader slow-load identity=ba0d8842fa53e521 stage=5 frame=402084 monotonic_begin_ns=36521097643944 monotonic_end_ns=36521106862175 total_ms=9.22
3017720: [engine] NVN typed storage UNSUPPORTED shader=ba0d8842fa53e521 binding=53 format=100 reason=rewrite-refused shared=0 unchanged=1
3017721: [engine] FATAL: Texel format lacks formatless-write support
3017722: [engine] FATAL: Texel format lacks formatless-write support
```

`shared=0` is the old rule's "the shared image type may not carry this
format" flag: the group of bindings that share the type does not agree on one
format, so `xbg_typed_storage_rewrite()` refused and left the module
byte-for-byte unchanged. The module then still had to perform a formatless
storage write, which the device cannot do.

### Module evidence (the real module from the game package)

The module is taken from the game package (`FFRS-data.zip`, entry
`FFRS/romfs/shaders/ba0d8842fa53e521.xgs`, 704 byte container header) and is
**byte-identical** to the module the device ran:

| file | sha256 |
| --- | --- |
| `.local/modules/ba0d8842fa53e521.spv` (from the package) | `be3e451350df384bfcf2699e016e284bd518766c042f263e25ddae23a5385e6f` |
| device-side copy `ff-resonance-rg477m-native/.local/modules/ba0d8842fa53e521.spv` | `be3e451350df384bfcf2699e016e284bd518766c042f263e25ddae23a5385e6f` |

Its shape (parsed from the binary, 19145 words, bound 4413):

| item | value |
| --- | --- |
| `%48 = OpTypeImage %float Buffer 0 0 0 2 Unknown` | one formatless storage Buffer image, 9 words (format operand at +8) |
| `%49 = OpTypePointer UniformConstant %48` | one pointer type for it |
| `%51 = OpVariable %49 UniformConstant` | **binding 53** |
| `%53 = OpVariable %49 UniformConstant` | **binding 59** |
| `%44 = OpTypeImage %uint Buffer 0 0 0 2 R32ui` | binding 17, already typed, **not** a sharer of `%48` |
| writes | 49 `OpImageWrite`: 47 on binding 53, 2 on binding 59, every texel a `%v4float` (4 components) |
| reads | none on `%48`: the module's only `OpImageRead` and both `OpImageTexelPointer` are on `%44` (binding 17) |

So the two bindings that share `%48` both only **write**, both write four
float components, and they are the only users of that type.

## 2. The fix

### Rule

A shared storage image type is split when the bindings that need the typed
form do not agree on the format: every binding of the group receives a
**private copy** of the image type (same operands, its own Image Format), a
**private `OpTypePointer UniformConstant`**, and its `OpVariable` and the
`OpLoad`s from it are retargeted to the new type. No value, coordinate, texel
or write changes.

The refusal rules are kept:

* a binding that shares the image type and does **not** need the typed form
  would keep the shared type formatless → the **whole group is refused** and
  the module stays byte-for-byte unchanged;
* a formatless read on the same image keeps the module formatless (the split
  only accepts a binding whose loaded image is used by `OpImageWrite` alone);
* the sampled type must match the format's scalar class (VUID
  `StandaloneSpirv-Image-04965`);
* the texel must be at least as wide as the declared format (VUID
  `vkCmdDispatch-OpImageWrite-04469` / `08795`);
* a refused rewrite leaves the module byte-for-byte unchanged and is logged in
  the existing format (`reason=rewrite-refused`, `shared=0/1`, `unchanged=1`).

### Files

| File | Change |
| --- | --- |
| `src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h` | `xbg_spirv_image_format_components()`, `xbg_spirv_image_type_users()`, `xbg_spirv_storage_min_write_components()`, `xbg_spirv_shared_image_group()`, `xbg_typed_storage_split_rewrite()`, `xbg_typed_storage_declare()` (group level: same format → shared declaration, different formats → split, missing sharer → refuse) |
| `src/graphics/nvn_vulkan/engine/xbg_game.cpp` | `typed_storage_variant()` now calls `xbg_typed_storage_declare()` with all the candidates; the refusal log keeps its format, a new `NVN typed storage split shader=… binding=… format=…` line reports a split |
| `tests/device/test_typed_storage_split.cpp` | **new**: the split contract on the real module (guardrail, group refusal, same-format group, split, unchanged writes) |
| `tests/device/test_typed_storage_real_texel.cpp` | the shared-module section finds the group in the module itself, so both real shared modules run it |
| `scripts/verify_host.py` | the split contract step, `spirv-val` on the rewritten module, `FFRS_REAL_SPLIT_MODULE` next to the three existing env vars |

## 3. Byte equivalence

The split changes **only** which format the module declares for a descriptor.
It changes no bound view, no coordinate, no texel and no write.

1. **The bound view is the same in both forms.** The descriptor path builds the
   `VkBufferView` from `texel_binding_format()`, which is the same function
   for the base module and for the typed variant. The split runs after that
   decision and never touches it, so the format, the offset, the range and the
   stride of the view are identical in the formatless and in the typed form.
2. **The texel layout is a property of the view's format, not of the shader's
   declaration.** `OpImageWrite` to a `VkBufferView` is constrained by
   *VUID-vkCmdDispatch-OpImageWrite-04469*: "If a VkBufferView is accessed using
   OpImageWrite as a result of this command, then the Type of the Texel operand
   of that instruction must have at least as many components as the buffer
   view's format." The rule makes the **view's** format the unit that decides
   how many components are consumed; the shader may not write fewer, and the
   components beyond the format's are not part of the view's texel. The
   shader's Image Format only has to agree with the view's numeric type
   (*VUID-vkCmdDispatch-format-07753*: "the numeric type of the view's format
   and the Sampled Type operand of the OpTypeImage must match") — it does not
   change the view, the layout, or the number of written components. Vulkan
   converts on the write ("Texel Encode", Vulkan 1.3 spec, *Image Format and
   Type Matching*), and the resource is the view in both forms.
3. **The declared format matches the bound format exactly.** Vulkan spec table
   118 (*SPIR-V and Vulkan Image Format Compatibility*) maps `R32f` to
   `VK_FORMAT_R32_SFLOAT` and `Rgba32f` to `VK_FORMAT_R32G32B32A32_SFLOAT`;
   SPIR-V's Image Format enumeration (SPIR-V 1.6, 3.2.10) lists both with the
   plain `Shader` capability. `spirv-val --target-env vulkan1.1` accepts
   both on a `Dim = Buffer` image of this module (measured, see section 4).
4. **The write itself is untouched.** The rewritten module differs from the
   original in exactly three ways (measured by disassembling both and
   diffing, section 4): the shared type's Image Format, the new private type
   and its pointer type, and the result type of the loads of the split
   binding. Every `OpImageWrite`, every `OpCompositeConstruct` (the texel
   values) and every coordinate instruction is **byte-identical**, and the host
   contract asserts it.

Therefore the split writes exactly the bytes the formatless form would have
written to the same buffer view: same texel addresses, same stride, same
bytes per texel.

*Width check.* The module writes four float components to both bindings
(`%v4float`), so the declared formats are legal on the write side:
`R32f` needs 1 component (`4 >= 1`) and `Rgba32f` needs 4 (`4 >= 4`),
per *VUID-vkCmdDispatch-OpImageWrite-04469* and
*VUID-RuntimeSpirv-OpImageWrite-07112*. The engine's own candidate formats
are the bound view formats, and the refusal rule in section 2 keeps a binding
whose bound format cannot be declared (or whose texel is narrower than the
format) out of the rewrite.

*Bound formats.* Binding 53's candidate format is **100 =
`VK_FORMAT_R32_SFLOAT`** (from the device log line above, `binding=53
format=100`). Binding 59's bound view format is the one the engine
resolves at runtime; it is reported by the new `NVN typed storage split` log
line and by the variant identity (`…:stb1:53_100,59_…`). It was **not**
measured on the device in this session (see section 6).

## 4. Host verification

All commands run in the worktree. The three real modules are extracted from
the game package into `.local/modules/` (gitignored, test inputs only) and
injected through the environment variables.

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/shared-texel-split-20261113
FFRS_REAL_MODULE=$PWD/.local/modules/c11e2f23ff063e85.spv \
FFRS_REAL_TEXEL_MODULE=$PWD/.local/modules/3b6302a58b86a34a.spv \
FFRS_REAL_SHARED_MODULE=$PWD/.local/modules/9bee7f09d6c655c3.spv \
FFRS_REAL_SPLIT_MODULE=$PWD/.local/modules/ba0d8842fa53e521.spv \
python3 scripts/verify_host.py --output $PWD/.local/host-verification-split
```

Result: **exit 0**, every check green, including the pre-existing ones
(`module-load-info` with the unpacked `main.nso`, the runtime contracts,
the probe build). The typed-storage part of the run:

```
typed-storage-contract: return_code=0, expected=0, passed=True
typed-storage-val: return_code=0, expected=0, passed=True
typed-storage-buffer-val: return_code=0, expected=0, passed=True
typed-storage-real-contract: return_code=0, expected=0, passed=True
typed-storage-real-texel-contract: return_code=0, expected=0, passed=True
typed-storage-real-shared-contract: return_code=0, expected=0, passed=True
typed-storage-split-build: return_code=0, expected=0, passed=True
typed-storage-split-contract: return_code=0, expected=0, passed=True
typed-storage-split-val: return_code=0, expected=0, passed=True
verify_host exit=0
```

The three env vars keep their meaning (`FFRS_REAL_MODULE`,
`FFRS_REAL_TEXEL_MODULE`, `FFRS_REAL_SHARED_MODULE`); `FFRS_REAL_SPLIT_MODULE`
was added for the module that needs the split and falls back to
`FFRS_REAL_SHARED_MODULE`.

### The split contract on the real module

```bash
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -Wall -Wextra \
    tests/device/test_typed_storage_split.cpp -o .local/work/test-split
.local/work/test-split .local/modules/ba0d8842fa53e521.spv .local/work/split_out.spv
# shared image type of bindings 53 and 59: the type is split and every binding declares its own format PASS
# exit 0
spirv-val --target-env vulkan1.1 .local/work/split_out.spv
# exit 0
```

The same contract on the other real shared module (`9bee7f09d6c655c3`,
bindings 29/35) passes as well.

### What the rewrite changes, measured

`spirv-dis` of the original and of the rewritten module, diffed: 102 changed
lines, all of them type level.

```
< %48 = OpTypeImage %float Buffer 0 0 0 2 Unknown
> %48 = OpTypeImage %float Buffer 0 0 0 2 Rgba32f          <- binding 59 keeps the shared type
> %4413 = OpTypeImage %float Buffer 0 0 0 2 R32f            <- binding 53 gets a private copy
> %_ptr_UniformConstant_4413 = OpTypePointer UniformConstant %4413
< %cp_i_cb2_4A_ = OpVariable %_ptr_UniformConstant_48 UniformConstant
> %cp_i_cb2_4A_ = OpVariable %_ptr_UniformConstant_4413 UniformConstant
< %3605 = OpLoad %48 %cp_i_cb2_4A_                          <- the 47 loads of binding 53
> %3605 = OpLoad %4413 %cp_i_cb2_4A_
```

No `OpImageWrite`, no `OpCompositeConstruct` and no coordinate instruction
appears in the diff.

## 5. Mutation self-proof

The assertions are not tautologies: each mutation of the new rule turns the
contract red. The header is restored byte-for-byte afterwards.

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/shared-texel-split-20261113
bash .local/work/mutation.sh
```

| run | exit code |
| --- | --- |
| baseline (unmutated) | **0** (green) |
| mutant `no-split`: the split branch uses the plain rewrite instead | **134** (red) |
| mutant `no-load-retarget`: the split does not retarget the `OpLoad` result type | **134** (red) |
| mutant `no-sharer-refusal`: a sharer that is not a candidate no longer refuses the group | **134** (red) |
| after restore | **0** (green), `diff -q` reports the header identical |

## 6. Device acceptance

**BLOCKED — the RG477M is not attached to this host in this session.**

```
$ adb devices -l
List of devices attached
# (no device listed; the Honor tablet is not attached either)
```

The USB layer lists no Android device at all, so no install and no run could
be done. Nothing was sent to any device; the Honor tablet was not touched.

What is prepared and waiting for the device:

* APK: `.local/android-apk/ffrs-game-window-debug.apk`,
  sha256 `845d4884a52d27a9cdac8633f3bb1bfb2b4fcf1ea1dfbb8d66698852354aec4a`,
  built by `bash scripts/android/build_game_window_apk.sh` (exit 0) from this
  worktree.
* Install command (no uninstall, no data clear, no `adb kill-server`):
  `adb -s RG477M01445122 install -r <apk>`, run with the working directory
  inside `ff-resonance-rg477m-native`.
* Acceptance steps: launch, drive into a battle and to the battle result screen,
  then `engine.log.fatal` absent and the process alive.

Until that run happens, **no device PASS is claimed** for this change.

## 7. Not verified, and the risks

1. **The device acceptance is NOT_RUN** (the RG477M is not attached, section 6).
   The fix is verified on the host only: the contract on the real module, the
   byte-equivalence argument, the rewritten module passing `spirv-val`, and the
   mutation proof.
2. **Binding 59's bound view format was not observed on the device.** Binding
   53's candidate format is known from the crash log (100 =
   `VK_FORMAT_R32_SFLOAT`); binding 59's is resolved at runtime and is only
   reported by the new log line. The byte-equivalence argument of section 3
   holds for any bound format the engine resolves, because the view's format
   decides the layout in both forms — but the concrete value is unmeasured.
3. **The split only engages on a device that cannot do the formatless write.**
   On the Honor tablet the formatless write works, so the substitution and the
   split stay inert and the module keeps running formatless, byte-identical to
   1.0.9. That is reasoned, not measured: the tablet was not re-run and must
   not be touched from this task.
4. **A shared image type with a formatless read is still refused.** The split
   accepts a binding whose loaded image is only written; a read on the same
   image keeps the module formatless and the device would still need the
   formatless feature. No such module was observed.
5. **A group with a binding that does not need the typed form is refused** (the
   whole group), by design: the other binding would keep the shared type
   formatless. Such a module would still abort on the RG477M; none was
   observed.
6. **The frame rate is not an acceptance result** and nothing here claims
   otherwise.
7. **The bound view substitution of the earlier fix is unchanged.** Where the
   guest's own view format cannot be declared in SPIR-V, the engine still binds
   the module's own 32-bit RGBA form; that trade-off was accepted before this
   change and is not revisited here.
