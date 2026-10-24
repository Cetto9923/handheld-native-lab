# P-MM-25 — the NVN sampler object family (15 names)

Role: GRAPHICS_CHAIN_IMPLEMENTATION
Base: `work/rg477m-native` at `d598a9a`
Branch to create: `task/P-MM-25-sampler-object`

## Why this card exists

The guest stops here. The device run of 2026-10-25 (APK sha256
`1c8974cadce1842c19a72d3c8ab15fb7ee3f97b45ddd0b4afb755f549a942cee`,
transcript `.local/logs/p-mm-29-guest-transcript.log`) says:

```
[hostevent] nvnSamplerBuilderSetDefaults: builder=0x5a39e80630
[hostevent] nvnSamplerBuilderSetDevice: builder=0x5a39e80630 device=0x5639a03810
[hostevent] nvnSamplerBuilderSetBorderColor: builder=0x5a39e80630 color=0x5a39e80620
[hostevent] nvnSamplerBuilderSetMinMagFilter: builder=0x5a39e80630 min=0x2 mag=0x0
[hostevent] nvnSamplerBuilderSetMaxAnisotropy: builder=0x5a39e80630 anisotropy=1
[hostevent] nvnSamplerBuilderSetWrapMode: builder=0x5a39e80630 s=0x7 t=0x7 r=0x7
[hostevent] nvnSamplerBuilderSetLodBias: builder=0x5a39e80630 bias=0
[hostevent] nvnSamplerBuilderSetLodClamp: builder=0x5a39e80630 min=0 max=3.40282e+38
[hostevent] unimplemented NVN entry "nvnSamplerInitialize" was called (trap slot 71, caller lr=0x72130cfd3c)
[hostevent]   args x0=0x50f9a75328 x1=0x5a39e80630 x2=0x0 x3=0x0 x4=0xffffffffffffffff x5=0x5a39e7fe9f x6=0xa x7=0x3033363038653933
```

The builder family landed and the guest ran through all eight builder calls.
The next thing the guest wants is the sampler **object**: a snapshot of the
builder's parameters that the sampler pool registers and that the draw uses.

## The fifteen names

These are the names the guest itself resolves (the list is in the transcript,
it is the guest's own list and not an inventory of an SDK header).

| name | signature | where it comes from |
|---|---|---|
| `nvnSamplerInitialize` | `NVNboolean (NVNsampler *s, const NVNsamplerBuilder *b)` | reference, `hle_nvn.c:558` |
| `nvnSamplerFinalize` | `void (NVNsampler *s)` | reference, `hle_nvn.c:561` |
| `nvnSamplerSetDebugLabel` | `void (NVNsampler *s, const char *l)` | reference, `hle_nvn.c:1924` |
| `nvnSamplerGetMinMagFilter` | `void (const NVNsampler *s, NVNminFilter *mi, NVNmagFilter *ma)` | reference, `hle_nvn.c:1929` |
| `nvnSamplerGetWrapMode` | `void (const NVNsampler *s, NVNwrapMode *a, *b, *c)` | reference, `hle_nvn.c:1930` |
| `nvnSamplerGetLodClamp` | `void (const NVNsampler *s, float *lo, float *hi)` | reference, `hle_nvn.c:1931` |
| `nvnSamplerGetLodBias` | `float (const NVNsampler *s)` | reference, `hle_nvn.c:1932` |
| `nvnSamplerGetCompare` | `void (const NVNsampler *s, NVNcompareMode *m, NVNcompareFunc *f)` | reference, `hle_nvn.c:1930` (same line block) |
| `nvnSamplerGetBorderColor` | `void (const NVNsampler *s, float *c)` | reference, `hle_nvn.c:1931` (same line block) |
| `nvnSamplerGetMaxAnisotropy` | `float (const NVNsampler *s)` | reference, `hle_nvn.c:1933` |
| `nvnSamplerGetReductionFilter` | `NVNsamplerReduction (const NVNsampler *s)` | reference, `hle_nvn.c:1934` |
| `nvnSamplerCompare` | `NVNboolean (const NVNsampler *a, const NVNsampler *b)` | reference, `hle_nvn.c:1935` |
| `nvnSamplerGetBorderColori` | `void (const NVNsampler *s, int *c)` | **inferred** from the builder's `SetBorderColori(const NVNsamplerBuilder*, const int*)` |
| `nvnSamplerGetBorderColorui` | `void (const NVNsampler *s, uint32_t *c)` | **inferred** from the builder's `SetBorderColorui(const NVNsamplerBuilder*, const uint32_t*)` |
| `nvnSamplerGetDebugID` | not known | no signature in the vendored header and none in the reference |

Two of the fifteen have no signature that is confirmed, and one more is
inferred. Say so in the log and in the report. For `nvnSamplerGetDebugID`,
take the raw arguments and answer with the honest thing this runtime can
answer, and say in the log what it answered and why. The device has a
debug domain id generator (`nvnDeviceGenerateDebugDomainId`); if you use it,
say so, and if you do not, say that too.

## The rules of this runtime

- **The state lives on the host**, keyed by the guest's sampler address. The
  layout of an `NVNsampler` is a property of the Nintendo SDK, which this
  runtime does not have, so **nothing is written into the guest's sampler
  object**. The contract proves it with a 0xAA sentinel.
- A sampler is a **snapshot** of the builder. The builder's parameters are
  copied at the moment of `Initialize`; a later change of the builder does not
  change a sampler that was already made, and the getters answer with the
  sampler's own values.
- The builder's parameters live in `nvn_sampler_builder.c` and are not
  readable from outside today. **Add the accessor you need to that family**
  (a function that copies the builder's parameters into the sampler's own
  state), and say in the report what you added and why.
- The reference's `samp_par` keeps the border colour as four floats. This
  runtime's builder keeps the float, the int and the unsigned int forms
  separately, because the guest can set any of the three. The sampler
  snapshots all three and each getter answers with the form it belongs to.
  The reference's `nvnSamplerCompare` compares the floats, the filters, the
  wrap modes, the compare mode and function, the reduction, the lod range,
  the lod bias, the anisotropy and the border colour; follow it exactly and
  say which fields take part.
- Every refusal says what was refused and why: a sampler that was never
  initialized, a builder that was never prepared, an index outside the table.

## Write scope (yours, and nothing else)

- `src/graphics/nvn_device/nvn_sampler.c` (new)
- `src/graphics/nvn_device/nvn_sampler.h` (new)
- `src/graphics/nvn_device/nvn_sampler_builder.c` — **only** the accessor you
  add, and its comment
- `src/graphics/nvn_device/nvn_sampler_builder.h` — the declaration of that
  accessor
- `tests/native/nvn_sampler_contract.c` (new, see below)
- `scripts/runtime/selfproof_nvn_sampler.sh` (new)
- `scripts/runtime/make_nvn_sampler_variants.py` (new)

Not yours: `src/graphics/nvn_device/nvn_device.c` (the dispatch), the CMake
files, `scripts/runtime/build_guest_process_contract.sh`,
`tests/native/guest_process_contract.c`, and everything in `coordination/`
except your new report file. The coordinator wires the dispatch and the
build.

## The contract, in its own file

The contract file of the project is shared and the coordinator keeps it, so
your case goes into a **new file of your own**:
`tests/native/nvn_sampler_contract.c`, with its own `main`, its own
`--nvn-sampler` switch and the same discipline the cases in the main contract
file have:

- `expect_value` / `expect_condition` counting into a static `errors`, a
  `CHECK(!errors)` at the end, and one line of JSON on stdout that reports
  the error count and the counters, for example
  `{"nvn_sampler_errors":0,"names":15,"initialized":1,"finalized":1,"compared":1}`;
- the expectations are independent of the implementation's constants;
- the case drives the family through the guest's own lookup
  (`guest_process_resolve(0,"nvnBootstrapLoader")` and the lookup it
  returns), the way the existing `--nvn-builder` case does, and not by
  calling the implementation's functions directly;
- the helpers (`CHECK`, `expect_value`, `expect_condition`, `errors`) are
  static in the main contract file and cannot be shared, so copy their shape.

The coordinator adds the build wiring for your file and for the second
contract binary, so the host gate runs it. Your self proof script therefore
builds and runs **your own binary**, not the main contract binary. Look at
`scripts/runtime/selfproof_nvn_command_buffer.sh` for the shape and at
`scripts/runtime/build_guest_process_contract.sh` for the object list and the
compile flags; take the same objects and the same flags.

Your variant generator `scripts/runtime/make_nvn_sampler_variants.py` reads
your contract file, breaks one expectation for variant A, removes the final
gate and hardcodes the reported error count to 0 for variant B, and writes a
stub that lies for variant C. It must not write outside `.local/selfproof/`.

## Acceptance

1. The fifteen names resolve, resolve to the same entry point every time, and
   the family table holds each of them once.
2. The contract drives a real sampler through the guest's lookup: build a
   builder, set its parameters, initialize a sampler, read every parameter
   back, and check that the values are the ones that were set.
3. The guest's sampler object is not written: the 0xAA sentinel holds.
4. A sampler is a snapshot: changing the builder after `Initialize` does not
   change the sampler's getters.
5. `nvnSamplerCompare` answers true for two samplers with the same parameters
   and false when one parameter differs, and the case checks that the
   parameter that is changed is one that takes part in the comparison.
6. A three variant self proof: **A** the expectation is broken and the final
   gate stays -> red; **B** the same break, the gate removed and the reported
   error count hardcoded to 0 -> the false green that exits 0 while its stdout
   still reports zero errors; **C** the contract untouched and the family
   stops copying the builder's parameters -> red.
7. The report in `coordination/reports/` says what the device run showed, the
   signatures that are confirmed and the ones that are inferred, what you
   added to the builder family, and what is still **not** proven.

## What is not yours

- The device. Do not take `.local/agent-pm/BOOT_LOCK_MM_A`, do not run adb,
  do not build or install the APK. The device run after the family lands is
  the coordinator's.
- The texture object family, the texture view family and the submit and
  present path. They are the next cards.
- Any claim about a frame. There is no frame and this card does not make one.
  It moves the guest one family further.
