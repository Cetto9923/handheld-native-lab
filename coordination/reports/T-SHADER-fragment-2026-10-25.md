# T-SHADER — why the driver refuses the game's fragment modules, and the ones it accepts

Card: **P-MM-29**. Device: `RG477M01445122` (RG477M, Android 14,
`Mali-G615 MC6`, driver 184553472, device API 1.3.247, the probe asks for a
Vulkan **1.1** instance). Baseline: `work/rg477m-native` @ `971df74`.

Every number below is a measurement from that device unless the text says
**inference**. This card produced **no pixels of the game**: what it
produces is a fragment module the driver accepts, a pipeline that builds,
and one draw the GPU consumes.

---

## 0. The answer in one paragraph

The driver refuses the game's fragment modules because they declare
**`FragmentShaderPixelInterlockEXT`** and **`OpExtension
"SPV_EXT_fragment_shader_interlock"`**, and this device offers neither
(`fragment_shader_interlock_supported = false`, no
`VK_EXT_fragment_shader_interlock` in the device's extension list). Remove
the capability **and** the extension declaration and the same module builds
(`VK_SUCCESS`) and one draw is consumed by the GPU. The game's **vertex**
module is refused for a different reason: it declares **`Float64`**, and the
device's `shaderFloat64` is `false`. Remove `Float64` from the vertex module
and it builds.

The three interface defects P-MM-27 suspected are **not** what this driver
objects to: the `OpAccessChain` on a `v4float` interface variable is legal
SPIR-V, the `NoPerspective` mismatch between the two stages is accepted, and
a fragment stage that reads an input location the vertex stage never writes
is accepted too. All three are measured, each on its own, below.

**The coordinator's `fix_spirv_capabilities.py` is not sufficient on its
own.** It removes `OpCapability` instructions and nothing else. Dropping
`FragmentShaderPixelInterlockEXT` leaves the `OpExtension` behind, and the
driver still refuses the pipeline (measured: `W2`). The tool needs a way to
drop an extension declaration as well; `scripts/graphics/fix_spirv_fragment_interface.py`
(the tool this card adds) has `--drop-extension`.

---

## 1. What was measured

### 1.1 The modules

| key | stage | declared capabilities | extension |
|---|---|---|---|
| `9cb65be4f7d0d498` | vertex | Shader, GroupNonUniform{Ballot,Shuffle,Vote}, ImageBuffer, ImageGatherExtended, ImageQuery, SampledBuffer, **Float64**, DrawParameters | — |
| `2ae58c692fb7fbb8` | fragment | the same list **plus FragmentShaderPixelInterlockEXT** | **`SPV_EXT_fragment_shader_interlock`** |
| `174293a30c315aed` | fragment | as above | **`SPV_EXT_fragment_shader_interlock`** |
| `3fefb8e379a30f64` | fragment | as above | **`SPV_EXT_fragment_shader_interlock`** |
| `41b20b5f09269ad8` | compute | the vertex list **plus Float64** | — |
| `86affd63144799de` | compute | the vertex list **plus Float64** | — |

The correlation is exact and it is the whole answer: **the three fragment
modules are the only ones that declare the interlock capability and the
interlock extension, and they are the only ones the driver refuses.** The
vertex module and the two compute modules do not declare them, and they
build.

### 1.2 The device's own facts (from the probe's tier 1, same run)

| fact | value |
|---|---|
| `shaderFloat64` supported / enabled | **false** / false |
| `fragment_shader_interlock_supported` | **false** |
| `VK_EXT_fragment_shader_interlock` in the extension list | **absent** |
| `VK_KHR_shader_subgroup_shuffle` in the extension list | absent |

(`VK_KHR_shader_subgroup_shuffle` is absent too, but it is **not** the
reason: the modules use no subgroup instruction. The opcodes that look like
subgroup ones are `156 = OpIsNan` and `169 = OpSelect`.)

### 1.3 The experiments

Every row is **its own `vkCreateGraphicsPipelines` call with its own module
pair**, in one probe run, so a rejected pipeline says nothing about its
neighbour. `draw consumed` means `vkQueueSubmit` and `vkWaitForFences` both
returned `VK_SUCCESS` and the probe's own draw was retired by the GPU.

**Run A — is the vertex module accepted at all?** (`.local/shader-verify/pm29/exp04-device/`)

| pipeline | fragment | vertex | result |
|---|---|---|---|
| `A1-vert-base-trivial` | trivial | game vertex, untouched | **-3** |
| `A2-vert-capfix-trivial` | trivial | game vertex, `Float64` removed | **VK_SUCCESS** |
| `A3-vert-capfix2-trivial` | trivial | game vertex, every unused capability removed | **VK_SUCCESS** |

The game vertex module is refused **because of `Float64` and nothing else**:
the same module with that one word removed builds. (P-MM-27's report says
"the game's vertex module builds"; that is true of the capability-stripped
module, not of the module as the translator emits it.)

**Run B — the extension is the decisive change** (`.local/shader-verify/pm29/exp08-device/`)

| pipeline | fragment | vertex | result |
|---|---|---|---|
| `Z1-noext-synnp` | `Float64`+FSIL capability+extension removed | synthetic vertex, `NoPerspective` | **VK_SUCCESS**, draw consumed |
| `Z2-noext-nonp-syn` | the same, `NoPerspective` also removed | synthetic vertex, smooth | **VK_SUCCESS**, draw consumed |
| `Z3-noext-nonp-vert` | the same | **the game's own vertex module** | **VK_SUCCESS**, draw consumed |
| `Z4-ctrl-capfix2nonp-syn` | every unused capability removed, **the extension still declared** | synthetic vertex | **-3** |
| `Z5-big1-noext-nonp-syn` | the first large fragment module, fixed | synthetic vertex | **VK_SUCCESS**, draw consumed |

`Z3` against `Z4` is the A/B that settles it: the two pipelines differ in
**one** thing — whether `OpExtension "SPV_EXT_fragment_shader_interlock"` is
still in the module — and the result flips from `-3` to `VK_SUCCESS`.

**Run C — which of the two, and does the interface matter?** (`.local/shader-verify/pm29/exp09-device/`)

| pipeline | what is changed | result |
|---|---|---|
| `W1-f64only-synnp` | only `Float64` removed; the interlock capability **and** the extension stay | **-3** |
| `W2-noextcap-synnp` | the extension removed, the interlock **capability** kept | **-3** |
| `W3-noext-vert` | capability **and** extension removed, `NoPerspective` kept, **the game's own vertex module** | **VK_SUCCESS**, draw consumed |
| `W4-noext-nonp-vert` | the same, `NoPerspective` removed from the fragment input | **VK_SUCCESS**, draw consumed |
| `W5-noext-vertnonp` | the same, `NoPerspective` put on the vertex output instead | **VK_SUCCESS**, draw consumed |
| `W6-big2-noext-nonp-syn` | the second large fragment module, fixed | **VK_SUCCESS**, draw consumed |
| `W7-big1-noext-nonp-vert` | the first large fragment module, fixed, **the game's own vertex module** | **VK_SUCCESS**, draw consumed |
| `F1`, `F2` | the two compute modules, capability-fixed (regression) | **VK_SUCCESS**, dispatch consumed |

Reading of run C:

* **Both halves of the interlock declaration have to go.** `W1` keeps both
  and is refused; `W2` keeps the capability and is refused; `W3` removes both
  and builds. Removing the capability alone (what the coordinator's tool
  does) is **not** enough.
* **The interpolation mismatch is not a blocker.** `W3` keeps the
  fragment's `NoPerspective` against the game vertex's smooth output and
  builds; `W4` removes it and builds; `W5` puts it on the vertex side
  instead and builds. All three are the same module as far as this driver
  is concerned.
* **A missing input location is not a blocker either.** `W7` builds the
  first large fragment module against the game's own vertex module, and the
  large fragment modules read `Input` location 1, which that vertex module
  never writes. See §4 — the pipeline builds, but that is a hole in the
  game's shader set, not something a module edit can close.

### 1.4 The three guesses P-MM-27 left, each measured

| P-MM-27's guess | measured |
|---|---|
| the `OpAccessChain` on a `v4float` interface variable is malformed | **not the reason.** It is legal SPIR-V (a component index into a vector), `spirv-val --target-env vulkan1.1` accepts it, and the delivered module keeps every one of them and builds. The indices `%int_0` and `%int_1` are **component** indices inside `Input` location 0, not two locations. |
| the fragment reads `Input` locations 0–1 while the vertex writes location 0 | **not a blocker.** `W7`: the fragment module that reads location 1 builds against the vertex module that writes only location 0. (It is still a real hole — §4.) |
| the `NoPerspective` decorations do not match | **not a blocker.** `W3` keeps the mismatch and builds. |

---

## 2. The delivered module and the script that makes it

The module the device accepts is the game's own fragment module with three
words removed: the `Float64` capability, the `FragmentShaderPixelInterlockEXT`
capability, and the `OpExtension "SPV_EXT_fragment_shader_interlock"`
declaration.

```bash
python3 scripts/graphics/fix_spirv_fragment_interface.py \
    --in  .local/shader-verify/spv/2ae58c692fb7fbb8.spv \
    --out .local/shader-verify/pm29/deliverable/2ae58c692fb7fbb8.fixed.spv \
    --drop-capability Float64 \
    --drop-capability FragmentShaderPixelInterlockEXT \
    --drop-extension SPV_EXT_fragment_shader_interlock \
    --report .local/shader-verify/pm29/deliverable/2ae58c692fb7fbb8.fixed.json
```

Exit code **0**. The capability removal is done by the coordinator's
`fix_spirv_capabilities.py`, which decides from the module's own evidence
and refuses to drop a capability the module uses; the extension removal and
the interface repairs are this card's tool.

| file | sha256 | on the device |
|---|---|---|
| `2ae58c692fb7fbb8.fixed.spv` (fragment) | `d3b5829d4c2755a08e54d9ce825a1565ec0730da2e4db7ff6f906ee7d675c8a7` | `W3` → `VK_SUCCESS`, draw consumed |
| `174293a30c315aed.fixed.spv` (fragment) | `43b0fe11f09ea2b25be1be80b0f7bcc7c7e2bf21049e7d52b0e37890ed3ea35d` | `W7` → `VK_SUCCESS`, draw consumed |
| `3fefb8e379a30f64.fixed.spv` (fragment) | `1552ec655a7c5092dde145f63c0c5a651693546c92bea1de46123cdb4023fc49` | `W6` → `VK_SUCCESS`, draw consumed |
| `9cb65be4f7d0d498.fixed.spv` (vertex) | `f16b7e0a1828989b1264abf8ac4ed95aa767a71326a7da6b59d502812e031326` | `A2`/`W3` → `VK_SUCCESS`, draw consumed |

The three fragment files are **byte-identical** to the `noext` variants the
device measured in `exp09-device/variants/noext/` (sha256 compared), so the
numbers above are numbers for these files. For the two large fragment
modules the `noext` and `noext_nonp` variants are themselves byte-identical
(those modules carry no `NoPerspective` decoration at all), so `W6` and `W7`
measured exactly the delivered bytes; for the small module `W3` is the
`noext` variant, which is the delivered file, and it keeps the
`NoPerspective` decoration.

**The original modules were not touched.** Their sha256 and their
modification times are unchanged (`10-25 00:18` / `00:19`, before this card
started); the tools read the input and write elsewhere.

| original | sha256 |
|---|---|
| `spv/2ae58c692fb7fbb8.spv` | `394eb9bb8bd2882eabfe4d342660685236599cbf48202d799320240154834048` |
| `spv/174293a30c315aed.spv` | `5998a79541c74ab06d57879146f6794676b999232f00ba25a6a14d2a7ce1e952` |
| `spv/3fefb8e379a30f64.spv` | `c090c434859fb603d17644d6729ad3c0559ff6812eea08b196ea4c12e08ce714` |
| `spv/9cb65be4f7d0d498.spv` | `93d5b6353de0e0579a6b1c1d1c2d6e772ad2c5f0d530b9957f2a6cd6bd94c55b` |
| `spv/41b20b5f09269ad8.spv` | `f4920b40f43f9f606963ac6366283c39b75c43e12a19a15d6590b9b01b8e3b71` |
| `spv/86affd63144799de.spv` | `ea55bc52fe2f2721c02e045dd0408619dad5d824c4d5a817ee2f6f704f3ec53c` |

### 2.1 The compute path did not regress

Re-run of the compute experiment in every run above, same manifest, same
device:

| pipeline | created | `vkCreateComputePipelines` | dispatch consumed |
|---|---|---|---|
| `41b20b5f09269ad8` (capability-fixed) | yes | **VK_SUCCESS** | **yes** |
| `86affd63144799de` (capability-fixed) | yes | **VK_SUCCESS** | **yes** |

Same as P-MM-23's and P-MM-27's tables. The compute modules never declared
the interlock capability, so they were never affected; the fix does not
touch them.

---

## 3. What this means for the runtime

1. **The translator emits capabilities and extensions the modules do not
   use.** The fix belongs there: a module that does not use the pixel
   interlock must not declare the interlock capability, and a module that
   does not declare the capability must not declare the extension either.
   The driver checks the declaration, not the use.
2. **`Float64` must not be declared.** The device's `shaderFloat64` is
   `false`; the vertex module is refused for that one word alone.
3. **`fix_spirv_capabilities.py` needs one more option.** It removes
   `OpCapability` instructions only. The `OpExtension` declaration has to
   go as well, or the pipeline stays refused (measured `W2`). This card did
   **not** touch that file — it is the coordinator's. The option this card
   needed is `--drop-extension`, and
   `scripts/graphics/fix_spirv_fragment_interface.py` has it.
4. **The runtime's own modules are still refused.** The modules the runtime
   hands to the driver are the translator's output; this card fixed copies
   in `.local/`, not the product. Nothing in the runtime path changes until
   the translator stops emitting the unused declarations (or the runtime
   applies the same correction before `vkCreateShaderModule`).

---

## 4. Not established

* **No frame, no title screen, no game rendering, no game pixel.** This
  card produced a module, a pipeline and a draw of the *probe's own* 8×8
  off-screen image. Nothing here says the game runs or draws.
* **A built pipeline is not a correct pipeline.** `W7` builds a fragment
  module that reads `Input` location 1 against a vertex module that writes
  only location 0, and the two large fragment modules write five output
  locations (0, 1, 2, 3, 5) against a render pass with one colour
  attachment. The driver accepts all of that; the values the fragment reads
  at location 1 are undefined. The hole is in the game's shader set — one
  vertex module for three fragment modules — and **no module edit closes
  it**. The translator still has to produce the vertex shaders that feed
  these fragment modules.
* **The instance version was not varied.** The probe asks for Vulkan 1.1 and
  that is what every number here was measured with. A Vulkan 1.3 instance
  was not tried. The fix does not depend on it: the declaration is removed
  rather than satisfied.
* **No validation layer.** `VK_LAYER_KHRONOS_validation` is not on this
  device, so no validation-layer diagnostic exists for any of this; the
  driver says nothing in logcat either (measured: the logcat of a run holds
  no pipeline diagnostic).
* **The `OpAccessChain` form was not compared against an alternative.** The
  measurement says the driver accepts it, not that it is the best form.
* **One driver build on one device.** Nothing here generalises to other
  Mali parts or other driver versions.
* **The coordinator's tool was not modified.** Its refusal to drop a
  capability a module uses still stands; this card used it, never changed
  it.

---

## 5. Files

Written, in this card's scope:

* `scripts/graphics/fix_spirv_fragment_interface.py` — **new**. Reads a
  fragment module's interface out of the module, reports the mismatches
  against a vertex module, and repairs the fragment module: drop the
  capabilities it does not use (through the coordinator's tool), drop an
  extension declaration, take `NoPerspective` off an input, put it on a
  vertex output, strip the entry point's body, strip the descriptor
  variables (for bisection).
* `scripts/graphics/fix_spirv_experiment.py` — **new**. Builds one module
  variant per hypothesis, puts every variant in front of the driver in one
  probe run, and writes the per-pipeline return codes out. It also emits
  the synthetic vertex modules used as test scaffolding.
* `coordination/reports/T-SHADER-fragment-2026-10-25.md` — this report.

**Not touched:** `src/graphics/nvn_device/**`,
`src/android/native/CMakeLists.txt`,
`scripts/runtime/build_guest_process_contract.sh`, `src/runtime/**`,
`tests/native/guest_process_contract.c`,
`scripts/graphics/fix_spirv_capabilities.py`,
`src/graphics/vulkan_probe/vulkan_shader_probe.c` (the stage-structure fix
from P-MM-27 was already in the baseline and was not needed again),
`src/graphics/vulkan_probe/vulkan_graphics_bisect.c`.

Evidence, under `.local/shader-verify/pm29/` (not committed):

```
exp04-device/          run A: the vertex module on its own
exp05-offline/         the first bisection's variants (no device run)
exp06-device/          the body/descriptor bisection (one pairing was wrong)
exp07-device/          the corrected body/descriptor bisection
exp08-device/          run B: the extension is the decisive change
exp09-device/          run C: which of the two, and the interface
deliverable/           the four delivered modules and their reports
```

Each run directory holds `run.json` (the parsed result),
`device-stdout.txt` (the probe's own JSON), `device-stderr.txt` (the
probe's trace), `build/manifest.txt` (the pipeline list) and
`variants/` (every module that was sent).

### 5.1 The device lock

`.local/agent-pm/BOOT_LOCK_MM_A` already existed when this card started,
dated `02:59`, i.e. from **P-MM-27**, which is finished and whose report is
the baseline of this card. It is the *MiniMax* lock — the very file this
card is told to create — and no other `BOOT_LOCK_*` file existed. The only
concurrent device user was the coordinator's own P-MM-33 run, which ended at
`08:54:38`, and the device was checked for activity before each of the five
device runs above. The lock was refreshed, its ownership recorded in
`.local/agent-pm/BOOT_LOCK_MM_A.owner`, and removed at the end of the card.
