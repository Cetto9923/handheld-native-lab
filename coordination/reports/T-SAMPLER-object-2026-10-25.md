# The NVN sampler object family (P-MM-25)

Date: 2026-10-25
Owner: worker (GRAPHICS_CHAIN_IMPLEMENTATION)
Branch: `task/P-MM-25-sampler-object`
Base: `work/rg477m-native` at `d598a9a`
Worktree: `.worktrees/p-mm-25` (the shared checkout carries other agents' WIP)
Commit: `becda29f782a265df96172a57a4e092694b10452` (the family, the accessor, the contract and the self proof)

**This card moves the guest one family further. It does not make a frame.**
No device was touched: the device is the coordinator's, the boot lock was not
taken, no adb was run, no APK was built or installed. There is no frame in
this report because there is none.

## What the device run showed (the input of this card)

`.local/logs/p-mm-29-guest-transcript.log` (APK sha256
`1c8974cadce1842c19a72d3c8ab15fb7ee3f97b45ddd0b4afb755f549a942cee`):

- the guest resolved the eight `nvnSamplerBuilder*` names and the builder
  family answered every one of them;
- the guest then resolved **fifteen** sampler names, in this order, and every
  one of them was a trap (slots 71 to 85):
  `nvnSamplerInitialize`, `nvnSamplerFinalize`, `nvnSamplerSetDebugLabel`,
  `nvnSamplerGetMinMagFilter`, `nvnSamplerGetWrapMode`, `nvnSamplerGetLodClamp`,
  `nvnSamplerGetLodBias`, `nvnSamplerGetCompare`, `nvnSamplerGetBorderColor`,
  `nvnSamplerGetBorderColori`, `nvnSamplerGetBorderColorui`,
  `nvnSamplerGetMaxAnisotropy`, `nvnSamplerGetReductionFilter`,
  `nvnSamplerCompare`, `nvnSamplerGetDebugID`;
- the first fatal was `nvnSamplerInitialize` with `x0=0x50f9a75328` (the
  sampler) and `x1=0x5a39e80630` (the builder the guest had just filled).

That list of fifteen is the guest's own list, not an inventory of a header.

## What landed

- `src/graphics/nvn_device/nvn_sampler.{h,c}` — the family. The state lives on
  the host, keyed by the address of the guest's sampler; **nothing is written
  into the guest's sampler object**, because the layout of an `NVNsampler`
  belongs to the Nintendo SDK, which this runtime does not have. A sampler is
  a **snapshot**: the builder's parameters are copied at the moment of
  `nvnSamplerInitialize`, and a later change of the builder does not touch a
  sampler that was already made.
- `tests/native/nvn_sampler_contract.c` — the contract, in a translation unit
  of its own, with its own `main` and its own `--nvn-sampler` switch.
- `scripts/runtime/selfproof_nvn_sampler.sh` and
  `scripts/runtime/make_nvn_sampler_variants.py` — the three variant self
  proof.

### The accessor that was added to the builder family

The builder's parameters were not readable from outside
`nvn_sampler_builder.c`, and the sampler needs them **at the moment of the
initialization** (asking the builder's getters one by one would answer with
the values the builder holds *now*, not with the values it held when the
sampler was made). So the builder family got one new entry point:

```c
int nvn_sampler_builder_copy_parameters(const void *builder, nvn_sampler_builder_parameters *out);
```

with the shape `nvn_sampler_builder_parameters` in `nvn_sampler_builder.h`.
It returns 1 when the builder was prepared and 0 when it was not, and the log
says which. The level of detail snap is **not** in that shape: the reference
host that runs this very game ignores it (`hle_nvn.c:548`), and the sampler
object has no getter for it. Nothing else of the builder family was touched.

## The signatures: what is confirmed and what is only inferred

The card lists two signatures as *inferred* and one as *not known*. Two of
the three are settled by evidence that the card did not have, and the third
is wrong in the card:

| name | signature | evidence |
|---|---|---|
| `nvnSamplerInitialize` | `boolean (NVNsampler*, const NVNsamplerBuilder*)` | vendored header `nvn.h:1391`, reference `nvn_protos.h:234`, reference body `hle_nvn.c:558` |
| `nvnSamplerFinalize` | `void (NVNsampler*)` | `nvn.h:1392`, `nvn_protos.h:235`, `hle_nvn.c:561` |
| `nvnSamplerSetDebugLabel` | `void (NVNsampler*, const char*)` | `nvn.h:1393`, `nvn_protos.h:236`, `hle_nvn.c:1924` |
| `nvnSamplerGetMinMagFilter` | `void (const NVNsampler*, NVNminFilter*, NVNmagFilter*)` | `nvn.h:1394`, `nvn_protos.h:237`, `hle_nvn.c:1926` |
| `nvnSamplerGetWrapMode` | `void (const NVNsampler*, NVNwrapMode*×3)` | `nvn.h:1395`, `nvn_protos.h:238`, `hle_nvn.c:1927` |
| `nvnSamplerGetLodClamp` | `void (const NVNsampler*, float*, float*)` | `nvn.h:1397`, `nvn_protos.h:239`, `hle_nvn.c:1928` |
| `nvnSamplerGetLodBias` | `float (const NVNsampler*)` | `nvn.h:1398`, `nvn_protos.h:240`, `hle_nvn.c:1929` |
| `nvnSamplerGetCompare` | `void (const NVNsampler*, NVNcompareMode*, NVNcompareFunc*)` | `nvn.h:1399`, `nvn_protos.h:241`, `hle_nvn.c:1930` |
| `nvnSamplerGetBorderColor` | `void (const NVNsampler*, float*)` | `nvn.h:1400`, `nvn_protos.h:242`, `hle_nvn.c:1931` |
| `nvnSamplerGetBorderColori` | `void (const NVNsampler*, int*)` | `nvn.h:1401`, `nvn_protos.h:243` — **the card had this as inferred; it is confirmed by the same two places** |
| `nvnSamplerGetBorderColorui` | `void (const NVNsampler*, uint32_t*)` | `nvn.h:1402`, `nvn_protos.h:244` — **the card had this as inferred; it is confirmed by the same two places** |
| `nvnSamplerGetMaxAnisotropy` | `float (const NVNsampler*)` | `nvn.h:1403`, `nvn_protos.h:245`, `hle_nvn.c:1932` |
| `nvnSamplerGetReductionFilter` | `NVNsamplerReduction (const NVNsampler*)` | `nvn.h:1404`, `nvn_protos.h:246`, `hle_nvn.c:1933` |
| `nvnSamplerCompare` | `boolean (const NVNsampler*, const NVNsampler*)` | `nvn.h:1405`, `nvn_protos.h:247`, `hle_nvn.c:1935` |
| `nvnSamplerGetDebugID` | `uint64_t (const NVNsampler*)` | `nvn.h:1406`, `nvn_protos.h:248` — **the card said "not known"; it is known** |

Two corrections to the card, with the evidence:

1. **`nvnSamplerGetDebugID` has a signature.** The card says there is none in
   the vendored header and none in the reference. The vendored header has it:
   `src/graphics/nvn/include/nvn/nvn.h:1406`
   `typedef uint64_t (*PFNNVNSAMPLERGETDEBUGIDPROC)(const NVNsampler*);`
   and the reference tree has the same one at
   `.local/refs/nxnative/dm/gfx/nvn/nvn_protos.h:248`. The reference host does
   not *implement* it (it is in its `names.txt` and in its thunks but not in
   `hle_nvn.c`), so the reference gives no value for it — but the signature
   is known, and it is implemented here as `uint64_t (const NVNsampler*)`.
   The value it answers with is the id this runtime hands out for the object,
   taken from the device's own debug domain id generator
   (`nvnDeviceGenerateDebugDomainId`) — the same source every other object of
   this runtime uses. A sampler that was never initialized has no id, so the
   answer is 0 and the log says why.
2. **The two "inferred" signatures are confirmed** by the same two places, so
   they are no longer inferences. They are implemented exactly as inferred.

**What is still not confirmed for this game:** every one of the fifteen. The
vendored header is a reconstruction of NVN 53.105 taken from **another game**
(Darksiders II) and the reference tree's `nvn_protos.h` is generated from
NVN's own headers. Two independent reconstructions agreeing is evidence, not
proof for *this* game's SDK, and nothing here has been seen on the device yet.

## The comparison: which fields take part

`nvnSamplerCompare` compares exactly what the reference compares
(`hle_nvn.c:1935`): the minification and the magnification filter, the three
wrap modes, the compare mode, the compare function, the reduction filter, the
level of detail range, the level of detail bias, the anisotropy and the border
colour as four floats.

Two fields of this runtime do **not** take part, and that is the reference's
doing: the int and the unsigned int form of the border colour (the
reference's `samp_par` holds the colour as four floats and nothing else,
`nvn_obj.h:49`) and the level of detail snap (ignored by the reference,
`hle_nvn.c:548`). The debug label and the debug id are not part of the state
of a sampler either. The contract checks both directions: a sampler whose
anisotropy differs is not equal (the anisotropy takes part), and a sampler
whose *int* border colour differs is equal (that form does not take part).

## The self proof

`scripts/runtime/selfproof_nvn_sampler.sh` (log:
`.local/logs/p-mm-25-sampler-contract-selfproof.log`). The script compiles the
runtime's objects from the sources of this checkout, builds the three variants
and **checks the exit code of each run**; it exits 0 only when all four runs
have the expected code.

| run | what was broken | expected | **measured** |
|---|---|---|---|
| baseline | nothing, the contract as it is | 0 | **exit 0**, `{"nvn_sampler_errors":0,"names":15,"initialized":4,"finalized":1,"compared":5,"gets":19,"refused":4,"live_samplers":3}` |
| A | one expectation (the wrap mode the guest set) | 1 | **exit 1**, `[check-fail] nvn-sampler-wrap-s-round-trips actual=0x6 expected=0x0` |
| B | the same break, the final gate removed, the reported error count hardcoded to 0 | 0 | **exit 0** while stdout still reports `nvn_sampler_errors:0` and the stderr holds the real `[check-fail]` line — the false green, measured |
| C | the contract untouched, the family stops copying the builder's parameters | 1 | **exit 1**, the round trip expectations fail (min filter `actual=0x0 expected=0x2`, lod bias, wrap modes, compare mode/function …) |

The dispatch of a family in `src/graphics/nvn_device/nvn_device.c` belongs to
the coordinator, and the contract resolves the names through the guest's own
lookup, so the self proof builds a **copy** of that file with the family wired
in (`.local/selfproof/nvn_device_with_sampler_family.c`, written by the
variant generator) and uses the copy for the four runs. The file in the tree
is not touched.

## What the coordinator has to wire

1. `src/graphics/nvn_device/nvn_device.c`: a family block for
   `nvn_sampler_family_count()` / `nvn_sampler_family_entry_at()`, with the
   shape of the other families. The copy the self proof uses is exactly that
   block.
2. `scripts/runtime/build_guest_process_contract.sh`: `nvn_sampler.c` in the
   source list, and the family contract compiled and run as the second
   contract binary (the `nvn_*_contract.c` glob is already in the manifest
   inputs).
3. `src/android/native/CMakeLists.txt`: `"${FFRS_GUEST_NVN_DIR}/nvn_sampler.c"`
   in the NVN source list, so that the APK carries the family.

## What is **not** proven

- **No device evidence.** The family has never run on the RG477M. The
  transcript that motivated this card is the guest's own, from before the
  family existed. Whether the guest gets past `nvnSamplerInitialize` on the
  device is not known.
- **No frame, no draw, no pixel.** The sampler is a state object that nothing
  consumes yet: the sampler pool stores the sampler's address, and there is
  no descriptor, no pipeline and no replay that reads a sampler's parameters.
  The values this family keeps are correct as state and are not yet used for
  anything.
- **The signatures are not confirmed for this game** (see above), and
  `nvnSamplerGetDebugID`'s *value* is this runtime's own id, not the value a
  real NVN would return.
- **`nvnSamplerInitialize` refuses a builder that was never prepared** and
  answers false. The reference always answers true
  (`hle_nvn.c:558`). This is the card's rule ("a builder that was never
  prepared" is a refusal that has to say so), and the guest's real call passes
  a builder it has just filled, but the difference is real and is logged.
- **A getter on a sampler that was never initialized answers with the
  defaults** and says in the log that it refused. The reference has no such
  case, because its samplers are always initialized.
- The three border colour forms are snapshotted separately because this
  runtime's builder keeps them separately; the reference has only the float
  form, so the int and unsigned int forms have **no reference counterpart** for
  their values.
