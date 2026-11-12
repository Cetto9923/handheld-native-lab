# OSS-NOTES — RG477M texel formatless-write fix

Task: RG477M-TEXEL-FORMATLESS-20261113 (RG477M / Anbernic, Mali-G615).
Scope: the storage texel buffer path of `src/graphics/nvn_vulkan/engine`
(`xbg_spirv_typed_storage.h`, `xbg_game.cpp`), the fix for
`FATAL: Texel format lacks formatless-write support`.

## 1. Sources not used

**No external implementation was borrowed for this task.** The fix was derived
from this repository's own code, from the real FFRS modules, and from the
Khronos specifications. Nothing was copied from an external project, so there
is no URL, revision or licence to list for an implementation.

## 2. Specifications the fix relies on (rules, not code)

- **SPIR-V `OpTypeImage` Image Format operand** — the set of formats an image
  type may declare. It contains no 8-bit and no 16-bit format, and a
  `Dim = Buffer` image may only carry `Rgba32f`, `Rgba32i` or `Rgba32ui`.
  This is the rule that makes a 16-bit storage view impossible to declare.
- **Vulkan `VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT`** — the
  feature a formatless storage write needs, and the one the Mali-G615 does not
  report for the bound format (`bufferFeatures=0x58`).

These two rules were **not fetched from the web during this task**; they are
the specification text the engine's existing comments
(`src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h`) already state.
The claims the fix depends on were verified on the host with
spirv-tools 1.4.363.0 (`spirv-as`/`spirv-val`):

- `OpTypeImage %uint Buffer 0 0 0 2 Rgba32ui` assembles and **passes
  `spirv-val --target-env vulkan1.1`** — the substituted form is valid SPIR-V.
  This is checked by the `typed-storage-buffer-val` step of
  `scripts/verify_host.py`.
- The repository's own table `xbg_spirv_image_format()` is the authority for
  which VkFormats have an SPIR-V image format; no new table was taken from
  anywhere else.

## 3. Community material deliberately not used

Search summaries, blog posts and aggregator pages about "Vulkan storage image
without format" and "Mali formatless storage image" were **not** used as
sources: they are not primary and cannot be pinned to a revision. The
specification rules above and the measured device behaviour were enough.

---
